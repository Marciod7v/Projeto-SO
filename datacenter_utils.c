#include "datacenter_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <errno.h>
#include <pthread.h>

VMType *VMType_exists(DataCenter *dc, const char* type_id){
	for(size_t i = 0; i < dc->num_vm_types; i++){
		if(strncmp(type_id, dc->vm_types[i].id, MAX_STRING_SIZE) == 0){
			return &dc->vm_types[i];
		}
	}
	return NULL;
}

/**
 * Checks if a reservation with the given id already exists in the Data Center.
 * 
 * @param dc Pointer to Data Center.
 * @param reservation_id Id of the reservation being checked.
 * 
 * @return 1 if the reservation exists.
 * @return 0 if the reservation does not exist.
 */	
static int reservation_exists(DataCenter *dc, const char* reservation_id){
	for(size_t i = 0; i < dc->num_reservations; i++){
		if(strncmp(reservation_id, dc->reservations[i].id, MAX_STRING_SIZE) == 0){
			return 1;
		}
	}
	return 0;
}

/**
 * Tells whether the reservation places at least one VM on the given server.
 *
 * @param reservation Pointer to the reservation.
 * @param index Index of the server in dc->servers (server id - 1).
 */
static int uses_server(const Reservation *reservation, size_t index) {
	for (size_t i = 0; i < reservation->items_count; i++) {
		if (reservation->items[i].server_id == index + 1)
			return 1;
	}
	return 0;
}

void reservation_lock_servers(DataCenter *dc, const Reservation *reservation) {
	// Sempre em ordem crescente: duas threads nunca podem ficar esperando uma pela outra.
	for (size_t i = 0; i < dc->num_servers; i++) {
		if (uses_server(reservation, i))
			pthread_mutex_lock(&dc->servers[i].lock);
	}
}

void reservation_unlock_servers(DataCenter *dc, const Reservation *reservation) {
	for (size_t i = dc->num_servers; i > 0; i--) {
		if (uses_server(reservation, i - 1))
			pthread_mutex_unlock(&dc->servers[i - 1].lock);
	}
}

int reservation_check_items(DataCenter *dc, Reservation *reservation) {
	int result = 0;
	size_t vms_count = 0;

	pthread_mutex_lock(&dc->registry_lock);

	if (reservation_exists(dc, reservation->id)) {
		fprintf(stderr, "Reservation with id %s already exists.\n", reservation->id);
		result = 1;
	}

	for (size_t i = 0; result == 0 && i < reservation->items_count; i++) {
		ReservationItem *item = &reservation->items[i];

		if (item->amount == 0) {
			fprintf(stderr, "Reservation item must request at least one VM.\n");
			result = 1;
			break;
		}

		VMType *vm_type = VMType_exists(dc, item->vm_type_id);

		if (vm_type == NULL) {
			fprintf(stderr, "VM of type %s does not exist\n", item->vm_type_id);
			result = 1;
			break;
		}

		if (item->server_id == 0 || item->server_id > dc->num_servers) {
			fprintf(stderr, "Invalid server in reservation.\n");
			result = 1;
			break;
		}

		if (item->amount > MAX_RESERVATION_VMS - vms_count) {
			fprintf(stderr, "Reached max of VMS for a reservation.\n");
			result = 1;
			break;
		}

		vms_count += item->amount;

		// Associa vm_type ao item para uso posterior. Os tipos de VM nunca são removidos
        // nem alterados, portanto, o ponteiro permanece válido após o desbloqueio.
		item->vm_type = vm_type;
	}

	pthread_mutex_unlock(&dc->registry_lock);

	return result;
}

int reservation_check_resources(DataCenter *dc, Reservation *reservation) {
	Resources available[dc->num_servers];
	size_t hosted_count[dc->num_servers];

	/* Copia o estado atual dos servidores utilizados pela reserva, para que
	   possamos simular a alocação. Os demais servidores não estão bloqueados,
	   portanto, não devem ser alterados. */
	for (size_t i = 0; i < dc->num_servers; i++) {
		if (!uses_server(reservation, i))
			continue;

		available[i] = dc->servers[i].available;
		hosted_count[i] = dc->servers[i].num_hosted_vms;
	}

	for (size_t i = 0; i < reservation->items_count; i++) {
		ReservationItem *item = &reservation->items[i];
		size_t s = item->server_id - 1;

		for (size_t j = 0; j < item->amount; j++) {
			if (hosted_count[s] >= MAX_HOSTED_VMS) {
				fprintf(stderr,
								"Server \"%zu\" reached the maximum number of hosted VMs.\n",
								item->server_id);
				return 1;
			}

			if (!resources_can_fit(available[s], item->vm_type->required)) {
				fprintf(stderr, "Server \"%zu\" does not have enough resources.\n", item->server_id);
				return 1;
			}

			resources_sub(&available[s], item->vm_type->required);
			hosted_count[s]++;
		}
	}

	return 0;
}

/**
 * Reverte as alterações feitas por uma operação de reserva.
 * 
 * @param reservation Pointer to the reservation being rolled back.
 * @param initial_vms Number of VMs that existed before the operation.
 */
static void reservation_rollback(Reservation *reservation, size_t initial_vms){
	while (reservation->num_vms > initial_vms) {
		VM *vm = reservation->vms[reservation->num_vms - 1];

		Server *server = vm->server;

		// Restaurar recursos
		resources_add(&server->available, vm->type->required);

		// Remover a VM da lista de máquinas hospedadas no servidor
		server->num_hosted_vms--;

		server->hosted_vms[server->num_hosted_vms] = NULL;

		// Remover VM da reserva
		reservation->num_vms--;

		reservation->vms[reservation->num_vms] = NULL;

		free(vm);
	}
}

int reservation_commit(DataCenter *dc, Reservation *reservation){
	// Keep track of how many vms existed before.
	size_t initial_vms = reservation->num_vms;

	for (size_t i = 0; i < reservation->items_count; i++) {
		ReservationItem *item = &reservation->items[i];
		Server *server = &dc->servers[item->server_id - 1];

		for (size_t j = 0; j < item->amount; j++) {
			VM *new_vm = calloc(1, sizeof(VM));
			if (new_vm == NULL) {
				fprintf(stderr, "Failed to allocate VM.\n");
				reservation_rollback(reservation, initial_vms);
				return 1;
			}

			snprintf(new_vm->id,
				MAX_VM_ID_STRING,
				"%s_%zu",
				reservation->id,
				reservation->num_vms + 1
			);

			new_vm->type = item->vm_type;
			new_vm->server = server;
			new_vm->dc = dc;
			new_vm->state = VM_STATE_RESERVED;

			resources_sub(&server->available, item->vm_type->required);

			server->hosted_vms[server->num_hosted_vms++] = new_vm;
			reservation->vms[reservation->num_vms++] = new_vm;
		}
	}

	reservation->state = RES_STATE_PENDING;

	return 0;
}

// Abortar a reserva
void reservation_abort(DataCenter *dc, Reservation *reservation) {
	reservation_lock_servers(dc, reservation);
	reservation_rollback(reservation, 0);
	reservation_unlock_servers(dc, reservation);
}

Reservation *find_pending_reservation(DataCenter *dc, const char *reservation_id) {
	Reservation *res = NULL;

	for (size_t i = 0; i < dc->num_reservations; i++) {
		if (strcmp(dc->reservations[i].id, reservation_id) == 0) {
			res = &dc->reservations[i];
			break;
		}
	}

	if (!res) {
		fprintf(stderr, "Reservation ID %s not found.\n", reservation_id);
		return NULL;
	}

	if (res->state != RES_STATE_PENDING) {
		fprintf(stderr, "Reservation ID %s is not in a pending state.\n", reservation_id);
		return NULL;
	}

	return res;
}

void reservation_destroy(DataCenter *dc, Reservation *reservation) {
	// Free every VM belonging to the reservation.
	for (size_t i = 0; i < reservation->num_vms; i++) {
		VM *vm = reservation->vms[i];
		Server *server = vm->server;

		// Return the VM resources back to the hosting server.
		resources_add(&server->available, vm->type->required);

		// Remove the VM from the server's hosted VM list.
		for (size_t j = 0; j < server->num_hosted_vms; j++) {
			if (server->hosted_vms[j] == vm) {
				// Shift the remaining VMs one position to the left.
				memmove(&server->hosted_vms[j],
								&server->hosted_vms[j + 1],
								(server->num_hosted_vms - j - 1) * sizeof(VM *));

				server->num_hosted_vms--;
				break;
			}
		}

		free(vm);
		reservation->vms[i] = NULL;
	}

	reservation->num_vms = 0;

	// Find the reservation inside the Data Center reservation list.
	size_t idx;
	for (idx = 0; idx < dc->num_reservations; idx++) {
			if (&dc->reservations[idx] == reservation)
				break;
	}

	// Reservation not found (should never happen).
	if (idx == dc->num_reservations)
		return;

	// Remove the reservation by shifting the remaining ones.
	memmove(&dc->reservations[idx],
					&dc->reservations[idx + 1],
					(dc->num_reservations - idx - 1) * sizeof(Reservation));

	dc->num_reservations--;
}
void vm_destroy(DataCenter *dc, VM *vm) {
	Server *server = vm->server;

	// Passo 1: liberar os recursos. É necessário apenas o bloqueio deste servidor, de modo que
	// VMs alocadas em servidores diferentes não fiquem aguardando umas pelas outras.
	pthread_mutex_lock(&server->lock);

	resources_add(&server->available, vm->type->required);

	for (size_t j = 0; j < server->num_hosted_vms; j++) {
		if (server->hosted_vms[j] == vm) {
			memmove(&server->hosted_vms[j],
							&server->hosted_vms[j + 1],
							(server->num_hosted_vms - j - 1) * sizeof(VM *));

			server->num_hosted_vms--;
			break;
		}
	}

	pthread_mutex_unlock(&server->lock);

	// Passo 2: remover a VM de sua reserva. Quando ela for a última,
	// a própria reserva é removida do Data Center. A VM só é
	// liberada aqui, com o registro bloqueado, pois a função datacenter_list()
	// pode ainda estar acessando-a por meio da reserva.
	pthread_mutex_lock(&dc->registry_lock);

	for (size_t i = 0; i < dc->num_reservations; i++) {
		Reservation *res = &dc->reservations[i];

		for (size_t k = 0; k < res->num_vms; k++) {
			if (res->vms[k] != vm)
				continue;

			memmove(&res->vms[k],
							&res->vms[k + 1],
							(res->num_vms - k - 1) * sizeof(VM *));

			res->num_vms--;
			res->vms[res->num_vms] = NULL;

			if (res->num_vms == 0) {
				res->state = RES_STATE_FINISHED;
				reservation_destroy(dc, res);
			}

			free(vm);
			pthread_mutex_unlock(&dc->registry_lock);
			return;
		}
	}

	free(vm); // Not part of any reservation (should never happen).
	pthread_mutex_unlock(&dc->registry_lock);
}

/*cria mais um parametro pois precisava acessar ao numero de servidores 
para calcular a percentagem de cpu.
*/
void spawn_vm_child(VM *vm, size_t num_servers) {
	
	// TODO: Limit RAM, DISK and use exec with cpulimit.
    double cpu_percentagem = vm->type->required.cpu/(vm->server->total.cpu*(double)num_servers)*100;
	char cpu_str[32];


	size_t ram_limit = vm->type->required.ram << 30;
    struct rlimit lim;
	lim.rlim_cur = ram_limit;
	lim.rlim_max = ram_limit;
	setrlimit(RLIMIT_AS,&lim);
	
	size_t disk_limit = vm->type->required.disk << 30;
	lim.rlim_cur = disk_limit;
	lim.rlim_max = disk_limit;
	setrlimit(RLIMIT_FSIZE,&lim);

	// O cpulimit não aceita menos de 1%: utilize 1% como mínimo.
	if (cpu_percentagem < 1.0)
		cpu_percentagem = 1.0;

	snprintf(cpu_str,sizeof(cpu_str),"%.2f",cpu_percentagem);
	execlp("cpulimit","cpulimit","-q","-f","-l",cpu_str,"--",vm->type->exec_path,NULL);

	// O cpulimit não está instalado: execute a aplicação sem o limite de CPU
	// (os limites de RAM e de disco já estão definidos), em vez de não a executar de forma alguma.
	if (errno == ENOENT) {
		fprintf(stderr, "cpulimit not found: running %s without CPU limit\n", vm->id);
		execl(vm->type->exec_path, vm->type->exec_path, (char *)NULL);
	}

	perror("exec spawn child");
	_exit(1);
    

}

/**
 * Waits for the process of a VM to end and reports an abnormal ending.
 * Does not touch the Data Center state, so it does not need the lock.
 *
 * @param vm Pointer to the VM whose process is being waited on.
 */
static void reap_vm(VM *vm) {
	int status = 0;
	pid_t result;

	do {
		result = waitpid(vm->pid, &status, 0);
	} while (result == -1 && errno == EINTR);

	if (result == -1) {
		perror("waitpid");
		return;
	}

	if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
		fprintf(stderr, "terminou com erro %d o %s\n", WEXITSTATUS(status), vm->id);
	} else if (WIFSIGNALED(status)) {
		fprintf(stderr, "terminou pelo sinal %d o %s\n", WTERMSIG(status), vm->id);
	}
}

/**
 * Chamado quando o processo de uma VM é encerrado: destrói a VM (liberando seus
 * recursos) e desperta a função datacenter_wait_all_vms() caso se trate da última VM.
 * Quem realiza a chamada não deve manter nenhum bloqueio (lock).
 */
static void vm_finished(DataCenter *dc, VM *vm) {
	vm_destroy(dc, vm);

	pthread_mutex_lock(&dc->running_lock);

	dc->running_vms--;

	if (dc->running_vms == 0)
		pthread_cond_broadcast(&dc->idle);

	pthread_mutex_unlock(&dc->running_lock);
}

/**
 * Body of the thread that watches one VM: waits for its process to end and
 * then destroys the VM, as soon as that happens.
 *
 * @param arg Pointer to the VM being watched.
 */
static void *vm_watcher(void *arg) {
	VM *vm = arg;
	DataCenter *dc = vm->dc;

	reap_vm(vm);
	vm_finished(dc, vm);

	return NULL;
}

int spawn_all_vms(DataCenter *dc, VM **vms, size_t count) {
	int failed = 0;

	for (size_t i = 0; i < count; i++) {
		VM *vm = vms[i];
		pid_t pid = fork();

		if (pid == -1) {
			perror("fork erro");
			vm_destroy(dc, vm); // Esta VM nunca será executada: libere seus recursos.
			failed = 1;
			continue;
		}

		if (pid == 0) {
			spawn_vm_child(vm, dc->num_servers);
			_exit(1); // Só é alcançado se spawn_vm_child falhar.
		}

		// pid e state são lidos por datacenter_list() sob o bloqueio do servidor.
		pthread_mutex_lock(&vm->server->lock);
		vm->pid = pid;
		vm->state = VM_STATE_RUNNING;
		pthread_mutex_unlock(&vm->server->lock);

		pthread_mutex_lock(&dc->running_lock);
		dc->running_vms++;
		pthread_mutex_unlock(&dc->running_lock);

		// Uma thread por VM, apenas para aguardar o seu processo e destruí-lo.
		pthread_t watcher;

		if (pthread_create(&watcher, NULL, vm_watcher, vm) != 0) {
			// Alternativa: nenhuma thread disponível, portanto, aguarde aqui.
			fprintf(stderr, "Failed to create watcher thread for VM %s.\n", vm->id);
			reap_vm(vm);
			vm_finished(dc, vm);
			failed = 1;
			continue;
		}

		pthread_detach(watcher);
	}

	return failed;
}

const char *vm_state_to_string(VMState state) {
    switch (state) {
        case VM_STATE_RESERVED:   return "RESERVED";
        case VM_STATE_RUNNING:    return "RUNNING";
        case VM_STATE_TERMINATED: return "TERMINATED";
        default:                  return "UNKNOWN";
    }
}

const char *res_state_to_string(ReservationState state) {
    switch (state) {
			case RES_STATE_PENDING:  return "PENDING";
			case RES_STATE_RUNNING:  return "RUNNING";
			case RES_STATE_FINISHED: return "FINISHED";
			default:                 return "UNKNOWN";
    }
}