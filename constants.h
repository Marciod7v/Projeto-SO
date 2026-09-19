#define MAX_STRING_SIZE 40
#define MAX_PATH_SIZE 1024

#define MAX_VM_TYPES           128
#define MAX_VM_ID_STRING       MAX_STRING_SIZE + 4 // 4 = "_" + 3 digits
#define MAX_HOSTED_VMS         128

#define MAX_RESERVATIONS       256
#define MAX_RESERVATIONS_ITEMS 32
#define MAX_RESERVATION_VMS    128

// Maximum number of .conf files allowed in the input directory.
#define MAX_CONF_FILES 256

// Root directory under which every VM workspace is created: 
// CLOUDIST_TMP_DIR/<reservation_id>/<vm_id>
#define CLOUDIST_TMP_DIR "/tmp/CloudIST"