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

  // Checks that touch the file system are done before taking the lock.
  if (!file_exists(type->exec_path)) {
    fprintf(stderr, "Invalid executable path.\n");
    return 1;
  }

  if (!path_exists(type->input_folder)) {
    fprintf(stderr, "Invalid input folder.\n");
    return 1;
  }

  pthread_mutex_lock(&dc->registry_lock);

  if(VMType_exists(dc, type->id) != NULL){
    pthread_mutex_unlock(&dc->registry_lock);
    fprintf(stderr, "VM type with id %s already exists.\n", type->id);
    return 1;
  }

  if(dc->num_vm_types >= MAX_VM_TYPES){
    pthread_mutex_unlock(&dc->registry_lock);
    fprintf(stderr, "Maximum of VM types reached.\n");
    return 1;
  }

  dc->vm_types[dc->num_vm_types++] = *type;

  pthread_mutex_unlock(&dc->registry_lock);

  return 0;
}

int datacenter_reserve(DataCenter *dc, Reservation *reservation) {
  if (!dc->configured) {
    fprintf(stderr, "Data Center needs to be configured!\n");
    return 1;
  }

  // 1. Check ids, types and servers (takes the registry lock by itself).
  if (reservation_check_items(dc, reservation) != 0)
    return 1;

  // 2. Resources: only the servers used by this reservation are locked, so
  //    reservations on different servers do not wait for each other.
  reservation_lock_servers(dc, reservation);

  if (reservation_check_resources(dc, reservation) != 0 ||
      reservation_commit(dc, reservation) != 0) {
    reservation_unlock_servers(dc, reservation);
    return 1;
  }

  reservation_unlock_servers(dc, reservation);

  // 3. Register the reservation. Another thread may have taken the same id
  //    (or filled the table) since step 1: then undo step 2.
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

  // 1. Claim the reservation (PENDING -> RUNNING) and take a copy of its VM
  //    list. From now on no other thread can execute it, and `res` is not
  //    used again: the table may be shifted when other reservations end.
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

  // 2. Exercise 1: copy the input folders. This is the slow part and it runs
  //    without any lock.
  if (prepare_vm_workspaces(res_id, vms, count) != 0) {
    // Put the reservation back as pending, so 'A' can be tried again.
    pthread_mutex_lock(&dc->registry_lock);

    for (size_t i = 0; i < dc->num_reservations; i++) {
      if (strncmp(dc->reservations[i].id, res_id, MAX_STRING_SIZE) == 0)
        dc->reservations[i].state = RES_STATE_PENDING;
    }

    pthread_mutex_unlock(&dc->registry_lock);
    return 1;
  }

  // 3. Exercise 2: launch the VMs and return right away. Each VM is destroyed
  //    by its own thread as soon as its process ends.
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

  // The registry stays locked during the whole listing (so reservations and
  // VM types cannot change); each server is locked only while it is printed.
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