#include "datacenter.h"
#include "datacenter_utils.h"

#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

void datacenter_init(DataCenter *dc) {
  dc->servers = NULL;
  dc->num_servers = 0;
  //local cores init 
  dc->local_cores = 0;
  
  dc->num_vm_types = 0;

  dc->num_reservations = 0;

  dc->configured = 0;

  dc->running_vms = 0;
  pthread_mutex_init(&dc->registry_lock, NULL);
  pthread_mutex_init(&dc->running_lock, NULL);
  pthread_cond_init(&dc->idle, NULL);
}

void datacenter_destroy(DataCenter *dc) {
  for (size_t i = 0; i < dc->num_servers; i++) {
    for(size_t j = 0; j < dc->servers[i].num_hosted_vms; j++){
      free(dc->servers[i].hosted_vms[j]);
    }
  }
  for (size_t i = 0; i < dc->num_servers; i++) {
    pthread_mutex_destroy(&dc->servers[i].lock);
  }
  free(dc->servers);

  pthread_cond_destroy(&dc->idle);
  pthread_mutex_destroy(&dc->running_lock);
  pthread_mutex_destroy(&dc->registry_lock);
}

int datacenter_configure(DataCenter *dc, size_t num_servers, Resources *resources){
  if(dc->configured){
    fprintf(stderr, "Data Center is already configured!\n");
    return 1;
  }

  if (num_servers == 0 || resources->ram == 0 || resources->disk == 0 || resources->cpu <= 0.0) {
    fprintf(stderr, "Invalid configuration values.\n");
    return 1;
  }

  dc->servers = (Server*)malloc(num_servers*sizeof(Server));

  if(dc->servers == NULL){
    fprintf(stderr, "Failed to allocate memory for servers.\n");
    return 1;
  }

  for (size_t i = 0; i < num_servers; i++) {
    dc->servers[i].id = i+1;
    dc->servers[i].total = *resources;
    dc->servers[i].available = *resources;
    dc->servers[i].num_hosted_vms = 0;
    pthread_mutex_init(&dc->servers[i].lock, NULL);
  }

  dc->num_servers = num_servers;
  dc->configured = 1;

  return 0;
}

int datacenter_define_VM(DataCenter *dc, VMType *type){
  if (!dc->configured) {
    fprintf(stderr, "Data Center needs to be configured!\n");
    return 1;
  }

// As verificações que acessam o sistema de arquivos 
// são realizadas antes de obter o bloqueio.
  if (!file_exists(type->exec_path)) {
    fprintf(stderr, "Invalid executable path.\n");
    return 1;
  }
  char tmp[MAX_PATH_SIZE];
  if(absolute_path(dc->vm_types->exec_path,tmp,sizeof(tmp)) != 0){
    fprintf(stderr,"Invalid Executable");
    return 1;
  }
  snprintf(dc->vm_types->exec_path,sizeof(tmp),"%s",tmp);
  if (!path_exists(type->input_folder)) {
    fprintf(stderr, "Invalid input folder.\n");
    return 1;
  }

  pthread_mutex_lock(&dc->registry_lock);   // pede a chave
  if(VMType_exists(dc, type->id) != NULL){
    pthread_mutex_unlock(&dc->registry_lock); // devolve, mais com erro
    fprintf(stderr, "VM type with id %s already exists.\n", type->id);
    return 1;
  }

  if(dc->num_vm_types >= MAX_VM_TYPES){
    pthread_mutex_unlock(&dc->registry_lock); // devolve outra saida com erro
    fprintf(stderr, "Maximum of VM types reached.\n");
    return 1;
  }

  dc->vm_types[dc->num_vm_types++] = *type;
  pthread_mutex_unlock(&dc->registry_lock);  // devolve saida normal

  return 0;
}

int datacenter_reserve(DataCenter *dc, Reservation *reservation) {
  if (!dc->configured) {
    fprintf(stderr, "Data Center needs to be configured!\n");
    return 1;
  }

  // Verifica IDs, tipos e servidores (obtém o bloqueio do registro por conta própria).
  if (reservation_check_items(dc, reservation) != 0)
    return 1;

  // Recursos: apenas os servidores usados ​​por esta reserva estão bloqueados, portanto
  // reservas em servidores diferentes não aguardam umas pelas outras.
  reservation_lock_servers(dc, reservation);

  if (reservation_check_resources(dc, reservation) != 0 ||
      reservation_commit(dc, reservation) != 0) {
    reservation_unlock_servers(dc, reservation);
    return 1;
  }

  reservation_unlock_servers(dc, reservation);

 // Registre a reserva. Outra thread pode ter usado o mesmo ID.
 // (ou preencheu a tabela) desde o passo 1: então desfaça o passo 2.
  pthread_mutex_lock(&dc->registry_lock);

  int id_taken = 0;
  for (size_t i = 0; i < dc->num_reservations; i++) {
    if (strncmp(dc->reservations[i].id, reservation->id, MAX_STRING_SIZE) == 0)
      id_taken = 1;
  }

  if (id_taken || dc->num_reservations >= MAX_RESERVATIONS) {
    pthread_mutex_unlock(&dc->registry_lock);
    reservation_abort(dc, reservation);

    if (id_taken)
      fprintf(stderr, "Reservation with id %s already exists.\n", reservation->id);
    else
      fprintf(stderr, "Max number of reservations reached\n");
    return 1;
  }

  dc->reservations[dc->num_reservations] = *reservation;
  dc->num_reservations++;

  pthread_mutex_unlock(&dc->registry_lock);

  return 0;
}

/**
 * Copies the input folder of every VM into its own workspace directory
 * (CLOUDIST_TMP_DIR/<reservation_id>/<vm_id>), so the files are ready before
 * the VM's application is executed. Takes no lock: it only reads fields of the
 * VMs that never change, and nobody can destroy them before they are launched.
 *
 * @param res_id Id of the reservation.
 * @param vms VMs whose workspaces are being prepared.
 * @param count Number of VMs.
 *
 * @return 0 on success.
 * @return 1 on error.
 */
static int prepare_vm_workspaces(const char *res_id, VM **vms, size_t count) {
  for (size_t i = 0; i < count; i++) {
    VM *vm = vms[i];

    char vm_dir[MAX_PATH_SIZE];
    int len = snprintf(vm_dir, sizeof(vm_dir), "%s/%s/%s", CLOUDIST_TMP_DIR, res_id, vm->id);
    //copiar o diretorio de cada vm e guardar
    snprintf(vm->dirtowork,sizeof(vm->dirtowork),"%s",vm_dir);
    if (len < 0 || (size_t)len >= sizeof(vm_dir)) {
      fprintf(stderr, "VM workspace path is too long for VM \"%s\".\n", vm->id);
      return 1;
    }

    if (copy_directory_recursive(vm->type->input_folder, vm_dir) != 0) {
      fprintf(stderr, "Failed to prepare workspace for VM \"%s\".\n", vm->id);
      return 1;
    }
  }

  return 0;
}

int datacenter_execute(DataCenter *dc, const char *reservation_id) {
  if(!dc->configured){
    fprintf(stderr, "Data Center needs to be configured!\n");
    return 1;
  }

  VM *vms[MAX_RESERVATION_VMS];
  size_t count;
  char res_id[MAX_STRING_SIZE];

  // Assuma a reserva (PENDENTE -> EM EXECUÇÃO) e crie uma cópia de sua VM list
  // A partir de agora, nenhuma outra thread pode executá-la e `res` não é
  // utilizada novamente: a mesa poderá ser alterada quando outras reservas terminarem.
  pthread_mutex_lock(&dc->registry_lock);

  Reservation *res = find_pending_reservation(dc, reservation_id);
  if (!res) {
    pthread_mutex_unlock(&dc->registry_lock);
    return 1;
  }

  res->state = RES_STATE_RUNNING;
  count = res->num_vms;
  memcpy(vms, res->vms, count * sizeof(VM *));
  snprintf(res_id, sizeof(res_id), "%s", res->id);

  pthread_mutex_unlock(&dc->registry_lock);

  // Copie as pastas de entrada. Esta é a parte lenta, e ela é executada sem nenhum bloqueio.
  if (prepare_vm_workspaces(res_id, vms, count) != 0) {
  // Coloque a reserva novamente como pendente, para que 'A' possa ser tentado mais uma vez.
    pthread_mutex_lock(&dc->registry_lock);

    for (size_t i = 0; i < dc->num_reservations; i++) {
      if (strncmp(dc->reservations[i].id, res_id, MAX_STRING_SIZE) == 0)
        dc->reservations[i].state = RES_STATE_PENDING;
    }

    pthread_mutex_unlock(&dc->registry_lock);
    return 1;
  }

  // Inicie as VMs e retorne imediatamente. Cada VM é destruída 
  // por sua própria thread, assim que seu processo termina.
  return spawn_all_vms(dc, vms, count);
}

void datacenter_wait_all_vms(DataCenter *dc) {
  pthread_mutex_lock(&dc->running_lock);

  while (dc->running_vms > 0)
    pthread_cond_wait(&dc->idle, &dc->running_lock);

  pthread_mutex_unlock(&dc->running_lock);
}

int datacenter_list(DataCenter *dc) {
  if (!dc->configured) {
    fprintf(stderr, "Data Center needs to be configured!\n");
    return 1;
  }

  // O registro permanece bloqueado durante toda a listagem (de modo que as 
  // reservas e os tipos de VM não possam ser alterados); 
  // Cada servidor é bloqueado apenas enquanto é exibido.
  pthread_mutex_lock(&dc->registry_lock);

  printf("=================================== DATA CENTER STATUS ===================================\n");
  printf("\n[ PHYSICAL SERVERS ] (%zu total)\n", dc->num_servers);
  printf("------------------------------------------------------------------------------------------\n");

  for (size_t i = 0; i < dc->num_servers; i++) {
    Server *sp = &dc->servers[i];
    pthread_mutex_lock(&sp->lock);

    Resources used = sp->total;
    resources_sub(&used, sp->available);

    printf(" Server #%zu\n", sp->id);
    printf("   ├─ Resources Total     : %6.2f vCPUs | %3zu GB RAM | %3zu GB Disk\n",
            sp->total.cpu, sp->total.ram, sp->total.disk);
    printf("   ├─ Resources Used      : %6.2f vCPUs | %3zu GB RAM | %3zu GB Disk\n",
            used.cpu, used.ram, used.disk);
    printf("   ├─ Resources Available : %6.2f vCPUs | %3zu GB RAM | %3zu GB Disk\n",
            sp->available.cpu, sp->available.ram, sp->available.disk);
    printf("   └─ Hosted VMs (%zu):\n", sp->num_hosted_vms);

    if (sp->num_hosted_vms == 0) {
      printf("        (None)\n");
    } else {
      for (size_t j = 0; j < sp->num_hosted_vms; j++) {
        VM *vm = sp->hosted_vms[j];
        const char *prefix = (j == sp->num_hosted_vms - 1) ? "└──" : "├──";

        printf("        %s VM ID : %-16s | Type: %-10s | State: %-10s",
                prefix,
                vm->id,
                vm->type ? vm->type->id : "Unknown",
                vm_state_to_string(vm->state));

        if (vm->state == VM_STATE_RUNNING) {
          printf(" | PID: %d", (int)vm->pid);
        }
        printf("\n");
      }
    }
    printf("\n");
    pthread_mutex_unlock(&sp->lock);
  }

  printf("[ DEFINED VM TYPES ] (%zu registered)\n", dc->num_vm_types);
  printf("------------------------------------------------------------------------------------------\n");
  if (dc->num_vm_types == 0) {
    printf(" (None)\n\n");
  } else {
      for (size_t i = 0; i < dc->num_vm_types; i++) {
        VMType vt = dc->vm_types[i];
        printf("  * Type ID: %-12s | Requirements: %.2f vCPU, %zu GB RAM, %zu GB Disk\n",
                vt.id, vt.required.cpu, vt.required.ram, vt.required.disk);
        printf("    ├─ Executable : %s\n", vt.exec_path);
        printf("    └─ Input Dir  : %s\n\n", vt.input_folder);
      }
      printf("\n");
  }

  printf("[ RESERVATIONS ] (%zu total)\n", dc->num_reservations);
  printf("------------------------------------------------------------------------------------------\n");
  if (dc->num_reservations == 0) {
    printf(" (None)\n\n");
  } else {
    for (size_t i = 0; i < dc->num_reservations; i++) {
      Reservation res = dc->reservations[i];
      printf("  * Reservation ID: %-12s | State: %-8s | Total VMs: %zu\n",
              res.id, res_state_to_string(res.state), res.num_vms);

      printf("    ├─ Requested Pairs:\n");
      for (size_t k = 0; k < res.items_count; k++) {
        printf("    │    • %-12s : %zu unit(s)\n", res.items[k].vm_type_id, res.items[k].amount);
      }

      printf("    └─ Allocated VMs:\n");
      if (res.num_vms == 0) {
        printf("         (None)\n");
      } else {
          for (size_t k = 0; k < res.num_vms; k++) {
            VM *vm = res.vms[k];
            const char *prefix = (k == res.num_vms - 1) ? "└──" : "├──";
            printf("         %s ID: %-15s | Host Server: #%zu\n",
                    prefix,
                    vm->id,
                    vm->server->id);
          }
      }
      printf("\n");
    }
  }

  printf("================================================================--------------------------\n\n");

  pthread_mutex_unlock(&dc->registry_lock);
  return 0;
}

void datacenter_wait(unsigned int delay_ms){
  struct timespec delay = (struct timespec){delay_ms / 1000, (delay_ms % 1000) * 1000000};
  nanosleep(&delay, NULL);
}