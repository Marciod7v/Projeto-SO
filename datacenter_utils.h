#ifndef DATACENTER_UTILS__H
#define DATACENTER_UTILS__H 
#include "datacenter.h"
/**
 * Checks if a VMType with the given id exists.
 * 
 * @param dc Pointer to Data Center.
 * @param type_id Id of VM type being checked.
 * 
 * @return Pointer to VMType if it exists.
 * @return NULL if VMType does not exist.
 */
VMType *VMType_exists(DataCenter *dc, const char* type_id);

/**
 * First half of the validation of a reservation. Locks registry_lock by
 * itself. Checks that the id is not used yet, that every item asks for at
 * least one VM, that its VM type exists and its server is valid, and that the
 * reservation does not exceed MAX_RESERVATION_VMS. Fills item->vm_type.
 * Does not look at the servers' resources.
 *
 * @param dc Data Center where the reservation will be validated.
 * @param reservation Reservation to validate.
 *
 * @return 0 if the items are valid.
 * @return 1 otherwise (the error is printed).
 */
int reservation_check_items(DataCenter *dc, Reservation *reservation);

/**
 * Locks the servers used by the reservation, in ascending server order.
 * Must be called after reservation_check_items() succeeded.
 */
void reservation_lock_servers(DataCenter *dc, const Reservation *reservation);

/**
 * Unlocks the servers locked by reservation_lock_servers().
 */
void reservation_unlock_servers(DataCenter *dc, const Reservation *reservation);

/**
 * Second half of the validation: simulates the placement of every VM using a
 * temporary copy of the available resources of the servers used. The Data
 * Center state is left unchanged. The caller must hold the locks of the servers
 * used by the reservation (reservation_lock_servers()).
 *
 * @return 0 if the reservation can be fully allocated.
 * @return 1 otherwise (the error is printed).
 */
int reservation_check_resources(DataCenter *dc, Reservation *reservation);

/**
 * Creates all VMs requested by the reservation, assigns them to their
 * designated servers, updates the server resource availability, and stores
 * references to the created VMs in both the reservation and the corresponding
 * server hosted VM list.
 *
 * The reservation is expected to have already passed validation before this
 * function is called, and the caller must hold the locks of the servers used. If an error occurs during the commit process, the
 * function performs a rollback of the changes made by this operation.
 *
 * @param dc Data Center where the reservation will be committed.
 * @param reservation Reservation to allocate.
 *
 * @return 0 if the reservation was successfully committed
 * @return 1 if an error occurred and the changes were rolled back.
 */
int reservation_commit(DataCenter *dc, Reservation *reservation);

/**
 * Undoes a reservation_commit() that cannot be registered (for example the id
 * was taken meanwhile by another thread). Locks the servers by itself.
 */
void reservation_abort(DataCenter *dc, Reservation *reservation);

/**
 * Looks up a reservation by id and checks that it is pending.
 *
 * @param dc Pointer to a Data Center.
 * @param reservation_id ID of the reservation to look up.
 *
 * @return Pointer to the Reservation if found and RES_STATE_PENDING.
 * @return NULL if not found or not in RES_STATE_PENDING (error is printed).
 */
Reservation *find_pending_reservation(DataCenter *dc, const char *reservation_id);

/**
 * Frees every VM allocated to the reservation, returns their resources to the
 * corresponding hosting servers, removes them from each server's hosted VM
 * list, and finally removes the reservation from the Data Center reservation
 * list.
 *
 * @param dc Pointer to the Data Center containing the reservation.
 * @param reservation Pointer to the reservation to destroy.
 */
void reservation_destroy(DataCenter *dc, Reservation *reservation);

/**
 * Runs in the child process after fork(): sets resource limits (RAM,
 * disk) and execs the VM's workload under cpulimit. Never returns
 * (either execs successfully or calls exit(1) on failure).
 *
 * @param vm Pointer to the VM being spawned.
 */
void spawn_vm_child(VM *vm,size_t num_servers);

/**
 * Destroys a single VM: returns its resources to the hosting server, removes
 * it from the server and from its reservation, and frees it. When it was the
 * last VM of the reservation, the reservation is removed from the Data Center.
 * Takes the locks it needs by itself; the caller must not hold any.
 *
 * @param dc Pointer to the Data Center the VM belongs to.
 * @param vm Pointer to the VM being destroyed (invalid after the call).
 */
void vm_destroy(DataCenter *dc, VM *vm);

/**
 * Forks one child per VM, calling spawn_vm_child() in each child, and starts
 * one thread per VM that waits for its process to end and then destroys the
 * VM. Returns right after launching the processes. The caller must not hold
 * any lock.
 *
 * @param dc Pointer to the Data Center.
 * @param vms VMs to launch (all of one reservation).
 * @param count Number of VMs in vms.
 *
 * @return 0 on success.
 * @return 1 if any VM could not be started.
 */
int spawn_all_vms(DataCenter *dc, VM **vms, size_t count);

/**
 * Converts a VM state to a human-readable string. Used by datacenter_list().
 *
 * @param state The VMState to convert.
 *
 * @return Static, human-readable string describing the state.
 */
const char *vm_state_to_string(VMState state);

/**
 * Converts a reservation state to a human-readable string. Used by
 * datacenter_list().
 *
 * @param state The ReservationState to convert.
 *
 * @return Static, human-readable string describing the state.
 */
const char *res_state_to_string(ReservationState state);

#endif // DATACENTER_UTILS__H