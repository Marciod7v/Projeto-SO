#ifndef DATACENTER__H
#define DATACENTER__H

#include <stdio.h>
#include <pthread.h>
#include <sys/types.h>

#include "resources.h"
#include "constants.h"
#include "filesystem.h"

typedef struct VMType{
  char id[MAX_STRING_SIZE];        // Id of VM type. eg: "V1"
  char input_folder[MAX_PATH_SIZE];  // Folder with input for executable
  char exec_path[MAX_PATH_SIZE];     // Path for executable
  Resources required;
} VMType;

typedef enum {
  VM_STATE_RESERVED,   // placed on a Server, process not started yet (waiting for 'A' command)
  VM_STATE_RUNNING,    // process launched (fork()/exec() called), `pid` is valid
  VM_STATE_TERMINATED  // process finished (the VM is destroyed right after)
} VMState;

typedef struct Server Server;
typedef struct DataCenter DataCenter;

typedef struct VM {
  char id[MAX_VM_ID_STRING]; // VM id. eg: "R1_V1_1"
  VMType *type;   // VM type.
  Server *server; // Server where the VM is placed.
  DataCenter *dc; // Data Center the VM belongs to (used by the thread that waits for the VM).
  pid_t pid;      // PID of executable file.
  VMState state;  // State of the VM.
} VM;

typedef struct Server {
	size_t id;		     // Server ID (1, 2, 3, ...)
	Resources total;     // Resources that Server has.
	Resources available; // Resources still available.

	VM *hosted_vms[MAX_HOSTED_VMS];
    size_t num_hosted_vms;

	pthread_mutex_t lock; // Protects available, hosted_vms and num_hosted_vms,
	                      // and the pid/state of the VMs hosted here.
} Server;

typedef enum {
    RES_STATE_PENDING,   // Reserved via 'R', waiting for 'A'
    RES_STATE_RUNNING,   // 'A' was called
    RES_STATE_FINISHED   // All VMs in this reservation have terminated
} ReservationState;

typedef struct ReservationPair {
    char vm_type_id[MAX_STRING_SIZE]; // VM type. e.g: "V1"       
    size_t amount;                    // Amount of VMs with the given type being reserved.
    size_t server_id;                 // Server where each VM will be running on.

    VMType *vm_type;                   // Used for search optimization.
} ReservationItem;

typedef struct Reservation {
	char id[MAX_STRING_SIZE];                       // id of reservation.

	ReservationItem items[MAX_RESERVATIONS_ITEMS];  // List of reservations items 
    size_t items_count;

	VM *vms[MAX_RESERVATION_VMS];                   // List of VMs in this reservation.
	size_t num_vms;                                 // Number of VMs in this reservation.
	ReservationState state;                         // State of the reservation.
} Reservation;

typedef struct DataCenter {
	Server *servers;			     // List of Servers.
	size_t num_servers;              // Number of Servers.

	VMType vm_types[MAX_VM_TYPES];
    size_t num_vm_types;

	Reservation reservations[MAX_RESERVATIONS];
    size_t num_reservations;

	int configured;		      // Wheter the datacenter is configured or not.

	// Synchronization (several threads use the Data Center at the same time).
	// Lock order: a thread never asks for a Server lock while it holds
	// registry_lock, except datacenter_list(), which takes them one at a time
	// in ascending order. Nobody asks for registry_lock while holding a Server
	// lock. Several Server locks are always taken in ascending server order.
	pthread_mutex_t registry_lock; // Protects vm_types, reservations and num_reservations.
	pthread_mutex_t running_lock;  // Protects running_vms.
	pthread_cond_t idle;           // Signaled when running_vms drops to 0.
	size_t running_vms;            // VMs whose process was launched and has not ended yet.
} DataCenter;

/**
 * Initializes a Data Center.
 *
 * @param dc DataCenter being initialized.
 */
void datacenter_init(DataCenter *dc);

/**
 * Destroys a Data Center.
 *
 * @param dc DatacCenter being destroyed.
 */
void datacenter_destroy(DataCenter *dc);

/**
 * Configures a given Data Center.
 *
 * @param dc Pointer to Datacenter being configured.
 * @param resources Pointer to resources of every PC on the Datacenter.
 *
 * @return 0 on success.
 * @return 1 on error.
 */
int datacenter_configure(DataCenter *dc, size_t num_pcs, Resources *resources);

/**
 * Defines a VM for the Data Center
 *
 * @param dc Pointer to a Data Center.
 * @param type Pointer to VM type.
 * 
 * @return 0 on success.
 * @return 1 on error.
 */
int datacenter_define_VM(DataCenter *dc, struct VMType *type);

/**
 * Reserves a set of VMs for a given reservation.
 *
 * @param dc Pointer to a Data Center.
 * @param reservation Pointer to a Reservation.
 * 
 * @return 0 on success.
 * @return 1 on error.
 */
int datacenter_reserve(DataCenter *dc, Reservation *reservation);

/**
 * Lists the current state of the Data Center.
 *
 * @param dc Pointer to a Data Center.
 * 
 * @return 0 on success.
 * @return 1 on error.
 */
int datacenter_list(DataCenter *dc);

/**
 * Executes a reservation, launching the VMs. Returns as soon as the processes
 * are launched; each VM is destroyed (and its resources returned) when its
 * process ends, and the reservation is removed when its last VM ends.
 *
 * @param dc Pointer to a Data Center.
 * @param reservation_id ID of the reservation to execute.
 * 
 * @return 0 on success.
 * @return 1 on error.
 */
int datacenter_execute(DataCenter *dc, const char *reservation_id);

/**
 * Blocks until every launched VM has ended and been destroyed.
 *
 * @param dc Pointer to a Data Center.
 */
void datacenter_wait_all_vms(DataCenter *dc);

/**
 * Executes a delay command.
 * 
 * @param delay_ms Delay in ms.
 */
void datacenter_wait(unsigned int delay_ms);

#endif // DATACENTER__H