#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include "parser.h"
#include "datacenter.h"
#include "constants.h"
#include "filesystem.h"

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

				printf("Finished reservation execution!\n");

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

int main(int argc, char **argv){
	DataCenter dc;
	datacenter_init(&dc);

	if (argc != 6) {
    fprintf(stderr, "Usage: %s <servers> <ram> <disk> <cpus> <input_dir>\n", argv[0]);
    return 1;
  }

	size_t servers;
	size_t ram;
	size_t disk;
	double cpu;

	if (parse_size_t_arg(argv[1], &servers) != 0 ||
			parse_size_t_arg(argv[2], &ram) != 0 ||
			parse_size_t_arg(argv[3], &disk) != 0 ||
			parse_double_arg(argv[4], &cpu) != 0) {
		fprintf(stderr, "Invalid command line arguments.\n");
		return 1;
	}

	const char *input_dir = argv[5];

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

	// Process every .conf file, in alphabetical order (guaranteed by
	// list_conf_files), sequentially.
	for (size_t i = 0; i < conf_files.count; i++) {
		int fd = open(conf_files.paths[i], O_RDONLY);

		if (fd < 0) {
			fprintf(stderr, "Failed to open file: %s\n", conf_files.paths[i]);
			continue;
		}

		process_commands(&dc, fd);

		close(fd);
	}

	datacenter_destroy(&dc);
	return 0;
}