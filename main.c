#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include "parser.h"
#include "datacenter.h"
#include "constants.h"
#include "filesystem.h"

typedef struct
{
    pthread_mutex_t caneta;
	size_t index;
	ConfFileList *conf_files;
	DataCenter *dc;
}Argspassados;


/**
 * Reads and processes every command available from fd, in order, until EOC
 * (end of commands / end of file) is reached.
 *
 * @param dc Pointer to the Data Center being operated on.
 * @param fd File descriptor to read commands from.
 */
static void process_commands(DataCenter *dc, int fd){
	while(1){
		switch (get_next_command(fd)){
			case CMD_DEFINE: {
				VMType vmtype;

				if (parse_define(fd, &vmtype) != 0) {
					fprintf(stderr, "Invalid define command. See H (help) for usage.\n");
					continue;
				}

				if(datacenter_define_VM(dc, &vmtype) != 0){
					fprintf(stderr, "Failed to define VM.\n");
					continue;
				}

				printf("VM successfully defined!\n");

				break;
			}

			case CMD_RESERVE: {
				Reservation reservation = {0};

				size_t num_items = parse_reserve(fd, &reservation, MAX_RESERVATIONS_ITEMS);

				if (num_items == 0) {
					fprintf(stderr, "Invalid reserve command. See H (help) for usage.\n");
					continue;
				}

				if (datacenter_reserve(dc, &reservation) != 0) {
					fprintf(stderr, "Failed to reserve VMs.\n");
					continue;
				}

				printf("Reservation made successfully!\n");

				break;
			}

			case CMD_EXECUTE:
				char id[MAX_STRING_SIZE];

				if(parse_execute(fd, id) != 0){
					fprintf(stderr, "Invalid execute command. See H (help) for usage.\n");
					continue;
				}

				if (datacenter_execute(dc, id) != 0) {
					fprintf(stderr, "Failed to execute reservation.\n");
					continue;
				}

				printf("Reservation execution started!\n");

				break;

			case CMD_LIST:
				if (datacenter_list(dc) != 0) {
					fprintf(stderr, "Failed to list VMs.\n");
					continue;
				}

				break;

			case CMD_WAIT:
				unsigned int delay;

				if(parse_wait(fd, &delay) != 0){
					fprintf(stderr, "Invalid wait command. See H (help) for usage.\n");
					continue;
				}

				datacenter_wait(delay);
				break;

			case CMD_INVALID:
				fprintf(stderr, "Invalid Command. See H (help) for usage.\n");
				break;

			case CMD_HELP:
				printf(
					"Spaces between arguments are allowed, but not after command end.\n"
					"Available commands:\n"
					" D <VM_TYPE_ID> <INPUT_FOLDER> <EXECUTABLE_PATH> <RAM_NEEDED> <DISK_NEEDED> <VCPU_NEEDED_COUNT>\n"
					" R <RESERVATION_ID> [<VM_TYPE_ID> <COUNT> <SERVER_ID>]+\n"
					" A <RESERVATION_ID>\n"
					" L\n"
					" E <DELAY_MS>\n"
					" H\n"
				);
				break;

			case CMD_EMPTY:
				break;

			case EOC:
				return;
		}
	}
}
static void *ordemdetrabalho(void *arg){
	Argspassados *mochila = arg;
	while(1){
		size_t i;
		pthread_mutex_lock(&mochila->caneta);
		if(mochila->index >= mochila->conf_files->count){
			pthread_mutex_unlock(&mochila->caneta);
			break;
		}
		i = mochila->index++;
		pthread_mutex_unlock(&mochila->caneta);

		const char *path = mochila->conf_files->paths[i];
		int fd = open(path, O_RDONLY);
		if (fd < 0){
			fprintf(stderr,"failed to open%s\n",path);
			continue;
		}
		process_commands(mochila->dc,fd);
		close(fd);
	}
	return NULL;
}

int main(int argc, char **argv){
	DataCenter dc;
	datacenter_init(&dc);
    // aumentamos o numero de argumentos que entram de 6 passa a ser 7 caso faltar emite erro
	if (argc != 8) {
    fprintf(stderr, "Usage: %s <servers> <ram> <disk> <cpus> <local_cores> <input_dir> <max_threads>\n", argv[0]);
    return 1;
  }

	size_t servers;
	size_t ram;
	size_t disk;
	//aqui metemos quantos threads vão ser lidos
	size_t max_threads;
	size_t local_cores;
	double cpu;

	if (parse_size_t_arg(argv[1], &servers) != 0 ||
			parse_size_t_arg(argv[2], &ram) != 0 ||
			parse_size_t_arg(argv[3], &disk) != 0 ||
			parse_double_arg(argv[4], &cpu) != 0 ||
			parse_size_t_arg(argv[5], &local_cores) != 0 ||
		    parse_size_t_arg(argv[7], &max_threads) != 0) {
		fprintf(stderr, "Invalid command line arguments.\n");
		return 1;
	}
	//caso seja 0 o valor de threads emite mensagme de erro
    if (max_threads == 0){
		fprintf(stderr, "Invalid max threads: %zu\n", max_threads);
		return 1;
	}
	if(local_cores == 0){
		fprintf(stderr,"Invalid local cores: %zu\n", local_cores);
		return 1;
	}
	dc.local_cores = local_cores;

	const char *input_dir = argv[6];

	if (!path_exists(input_dir)) {
		fprintf(stderr, "Invalid input directory: %s\n", input_dir);
		return 1;
	}

	Resources resources = {
    .ram = ram,
    .disk = disk,
    .cpu = cpu
	};

	if(datacenter_configure(&dc, servers, &resources) != 0){
		fprintf(stderr, "Failed to configure Data Center.\n");
		return 1;
	}

	ConfFileList conf_files;

	if (list_conf_files(input_dir, &conf_files) != 0) {
		fprintf(stderr, "Failed to list .conf files in %s\n", input_dir);
		datacenter_destroy(&dc);
		return 1;
	}
	//inicialização dos argumentos esatdo partilhado
	Argspassados mochila = {
		.index = 0,
		.conf_files = &conf_files,
		.dc = &dc,
	};
	pthread_mutex_init(&mochila.caneta, NULL);

	size_t num_threads = max_threads;
	//evitar criar mais threads desnecessário
	if(num_threads > conf_files.count){
		num_threads = conf_files.count;
	}
    pthread_t *threads = malloc(num_threads*sizeof(pthread_t));
	if(!threads){
		fprintf(stderr,"Failed to allocate threads arrays.\n");
		pthread_mutex_destroy(&mochila.caneta);
		datacenter_destroy(&dc);
		return 1;
	}

	size_t created = 0;
	//aqui se realiza o trabalho dinamico cada thread realiza seu trabalho 
	for(size_t t = 0; t < num_threads; t++){
      if(pthread_create(&threads[t], NULL, ordemdetrabalho,&mochila) != 0){
		fprintf(stderr,"Failed to create thread %zu\n",t);
		break;
	  }
	  created++;
	}
	//join seguro depois que todas as threads ja acabaram
	for(size_t t = 0; t< created; t ++){
		pthread_join(threads[t], NULL);
	}

	free(threads);
	pthread_mutex_destroy(&mochila.caneta);
	/*// Process every .conf file, in alphabetical order (guaranteed by
	// list_conf_files), sequentially.
	for (size_t i = 0; i < conf_files.count; i++) {
		
		int fd = open(conf_files.paths[i], O_RDONLY);

		if (fd < 0) {
			fprintf(stderr, "Failed to open file: %s\n", conf_files.paths[i]);
			continue;
		}

		process_commands(&dc, fd);

		close(fd);
	}*/

	// VMs run in the background: wait for all of them to end before cleaning up.
	datacenter_wait_all_vms(&dc);

	datacenter_destroy(&dc);
	return 0;
}