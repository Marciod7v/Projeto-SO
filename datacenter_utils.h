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

/**
* Valida ID, tipos, servidores e limites (pede o registry_lock)
*/
int reservation_check_items(DataCenter *dc, Reservation *reservation);

/**
 * Bloqueia os servidores utilizados pela reserva, em ordem crescente.
 * Deve ser chamada após a execução bem-sucedida de reservation_check_items().
 */
void reservation_lock_servers(DataCenter *dc, const Reservation *reservation);

/**
 * Desbloqueia os servidores que foram bloqueados por reservation_lock_servers().
 */
void reservation_unlock_servers(DataCenter *dc, const Reservation *reservation);

/**
 * Segunda parte da validação: simula a alocação de cada VM utilizando uma
 * cópia temporária dos recursos disponíveis dos servidores empregados.
 * O estado do data center permanece inalterado. Quem realiza a chamada deve deter os
 * bloqueios (locks) dos servidores utilizados pela reserva (reservation_lock_servers()).
 *
 * @return 0 if the reservation can be fully allocated.
 * @return 1 otherwise (the error is printed).
 */
int reservation_check_resources(DataCenter *dc, Reservation *reservation);

/**
 * Cria todas as VMs solicitadas pela reserva, atribui-as aos seus
 * servidores designados, atualiza a disponibilidade de recursos dos servidores e armazena
 * referências às VMs criadas, tanto na reserva quanto na lista de VMs hospedadas
 * no servidor correspondente.
 *
 * Espera-se que a reserva já tenha passado pela validação antes que esta
 * função seja chamada, e quem a invoca deve possuir os bloqueios (locks)
 * dos servidores utilizados. Se ocorrer um erro durante o processo de efetivação (commit),
 * a função realiza uma reversão (rollback) das alterações feitas por esta operação.
 *
 * @param dc Data Center where the reservation will be committed.
 * @param reservation Reservation to allocate.
 *
 * @return 0 if the reservation was successfully committed
 * @return 1 if an error occurred and the changes were rolled back.
 */
int reservation_commit(DataCenter *dc, Reservation *reservation);

/**
 * Desfaz uma operação `reservation_commit()` que não pode ser registrada 
 * (por exemplo, porque o ID foi obtido, nesse meio-tempo, por outra thread). 
 * Realiza, por conta própria, o bloqueio dos servidores.
 */
void reservation_abort(DataCenter *dc, Reservation *reservation);

/**
 * Busca uma reserva pelo ID e verifica se ela está pendente.
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
 * Destrói uma única VM: devolve seus recursos ao servidor hospedeiro, remove-a
 * do servidor e de sua reserva, e a libera. Caso seja a última VM da reserva,
 * a reserva é removida do Data Center.
 * Adquire por conta própria os bloqueios de que necessita; quem a invoca não deve manter nenhum bloqueio.
 *
 * @param dc Pointer to the Data Center the VM belongs to.
 * @param vm Pointer to the VM being destroyed (invalid after the call).
 */
void vm_destroy(DataCenter *dc, VM *vm);

/**
 * Cria um processo filho para cada VM, invocando `spawn_vm_child()` em cada um deles, e inicia
 * uma thread por VM que aguarda o término do respectivo processo para, em seguida, destruir a
 * VM. Retorna logo após a inicialização dos processos. Quem realiza a chamada não deve manter
 * nenhum bloqueio (lock).
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
 * Converte um estado de VM em uma string legível por humanos. Utilizado por datacenter_list().
 *
 * @param state The VMState to convert.
 *
 * @return Static, human-readable string describing the state.
 */
const char *vm_state_to_string(VMState state);

/**
* Converte um estado de reserva em uma string legível por humanos. Utilizado por
 * datacenter_list().
 *
 * @param state The ReservationState to convert.
 *
 * @return Static, human-readable string describing the state.
 */
const char *res_state_to_string(ReservationState state);

#endif // DATACENTER_UTILS__H