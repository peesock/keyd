/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 */

#include "keyd.h"

const char* SOCKET_PATH = SOCKET_PATH_DEFAULT;
const char* CONFIG_DIR = CONFIG_DIR_DEFAULT;
static int argcount = 1;

static int ipc_exec(int type, const char *data, size_t sz, uint32_t timeout)
{
	struct ipc_message msg;

	assert(sz <= sizeof(msg.data));

	msg.type = type;
	msg.sz = sz;
	msg.timeout = timeout;
	memcpy(msg.data, data, sz);

	int con = ipc_connect();

	if (con < 0) {
		perror("connect");
		exit(-1);
	}

	xwrite(con, &msg, sizeof msg);
	xread(con, &msg, sizeof msg);

	if (msg.sz) {
		xwrite(1, msg.data, msg.sz);
		xwrite(1, "\n", 1);
	}

	return msg.type == IPC_FAIL;

	return type == IPC_FAIL;
}

static int version(int argc, char *argv[])
{
	printf("keyd " VERSION "\n");

	return 0;
}

static int help(int argc, char *argv[])
{
	printf("usage: keyd [options] [<args>] [command] [<args>]\n\n"
	       "Commands:\n"
	       "    monitor [-t]                   Print key events in real time.\n"
	       "    list-keys                      Print a list of valid key names.\n"
	       "    reload                         Trigger a reload .\n"
	       "    listen                         Print layer state changes of the running keyd daemon to stdout.\n"
	       "    bind <binding> [<binding>...]  Add the supplied bindings to all loaded configs.\n"
	       "Options:\n"
	       "    -s, --socket <path>            Set socket path to <path>.\n"
	       "    -c, --config <path>            Set config dir to <path>.\n"
	       "    -v, --version                  Print the current version and exit.\n"
	       "    -h, --help                     Print help and exit.\n");

	return 0;
}

static int list_keys(int argc, char *argv[])
{
	size_t i;

	for (i = 0; i < 256; i++) {
		const char *altname = keycode_table[i].alt_name;
		const char *shiftedname = keycode_table[i].shifted_name;
		const char *name = keycode_table[i].name;

		if (name)
			printf("%s\n", name);
		if (altname)
			printf("%s\n", altname);
		if (shiftedname)
			printf("%s\n", shiftedname);
	}

	return 0;
}


static int add_bindings(int argc, char *argv[])
{
	int i;
	int ret = 0;

	for (i = 1; i < argc; i++) {
		if (ipc_exec(IPC_BIND, argv[i], strlen(argv[i]), 0))
			ret = -1;
	}

	if (!ret)
		printf("Success\n");

	return ret;
}

static void read_input(int argc, char *argv[], char *buf, size_t *psz)
{
	size_t sz = 0;
	size_t bufsz = *psz;

	if (argc != 0) {
		int i;
		for (i = 0; i < argc; i++) {
			sz += snprintf(buf+sz, bufsz-sz, "%s%s", argv[i], i == argc-1 ? "" : " ");

			if (sz >= bufsz)
				die("maximum input length exceeded");
		}
	} else {
		while (1) {
			size_t n;

			if ((n = read(0, buf+sz, bufsz-sz)) <= 0)
				break;
			sz += n;

			if (bufsz == sz)
				die("maximum input length exceeded");
		}
	}

	*psz = sz;
}

static int cmd_do(int argc, char *argv[])
{
	char buf[MAX_IPC_MESSAGE_SIZE];
	size_t sz = sizeof buf;
	uint32_t timeout = 0;

	if (argc > 2 && !strcmp(argv[1], "-t")) {
		timeout = atoi(argv[2]);
		argc -= 2;
		argv += 2;
	}

	read_input(argc-1, argv+1, buf, &sz);

	return ipc_exec(IPC_MACRO, buf, sz, timeout);
}


static int input(int argc, char *argv[])
{
	char buf[MAX_IPC_MESSAGE_SIZE];
	size_t sz = sizeof buf;
	uint32_t timeout = 0;

	if (argc > 2 && !strcmp(argv[1], "-t")) {
		timeout = atoi(argv[2]);
		argc -= 2;
		argv += 2;
	}

	read_input(argc-1, argv+1, buf, &sz);

	return ipc_exec(IPC_INPUT, buf, sz, timeout);
}

static int layer_listen(int argc, char *argv[])
{
	struct ipc_message msg = {0};

	int con = ipc_connect();

	if (con < 0) {
		perror("connect");
		exit(-1);
	}

	msg.type = IPC_LAYER_LISTEN;
	xwrite(con, &msg, sizeof msg);

	while (1) {
		char buf[512];
		ssize_t sz = read(con, buf, sizeof buf);

		if (sz <= 0)
			return -1;

		xwrite(1, buf, sz);
	}
}

static int reload()
{
	ipc_exec(IPC_RELOAD, NULL, 0, 0);

	return 0;
}

static int set_socket_path(int argc, char** argv)
{
	if (argc < 2){
		help(argc, argv);
		return 1;
	}
	SOCKET_PATH = argv[1];
	argcount++;
	return 64;
}

static int set_config_dir(int argc, char** argv)
{
	if (argc < 2){
		help(argc, argv);
		return 1;
	}
	CONFIG_DIR = argv[1];
	argcount++;
	return 64;
}

struct {
	const char *name;
	const char *flag;
	const char *long_flag;

	int (*fn)(int argc, char **argv);
} commands[] = {
	{"help", "-h", "--help", help},
	{"version", "-v", "--version", version},

	/* Keep -e and -m for backward compatibility. TODO: remove these at some point. */
	{"monitor", "-m", "--monitor", monitor},
	{"bind", "-e", "--expression", add_bindings},
	{"input", "", "", input},
	{"do", "", "", cmd_do},

	{"listen", "", "", layer_listen},

	{"reload", "", "", reload},
	{"list-keys", "", "", list_keys},
	{"", "-s", "--socket", set_socket_path},
	{"", "-c", "--config", set_config_dir},
};

int main(int argc, char *argv[])
{
	size_t i;

	log_level =
	    atoi(getenv("KEYD_DEBUG") ? getenv("KEYD_DEBUG") : "");

	if (isatty(1))
		suppress_colours = getenv("NO_COLOR") ? 1 : 0;
	else
		suppress_colours = 1;

	dbg("Debug mode activated");

	signal(SIGTERM, exit);
	signal(SIGINT, exit);
	signal(SIGPIPE, SIG_IGN);

	if (argc > 1) {
		int status = 0;
		_Bool exists = 0;
		for (; argcount < argc; argcount++) {
			for (i = 0; i < ARRAY_SIZE(commands); i++) {
				if (!strcmp(commands[i].name, argv[argcount]) ||
						!strcmp(commands[i].flag, argv[argcount]) ||
						!strcmp(commands[i].long_flag, argv[argcount])) {
					if ((status = commands[i].fn(argc - argcount, argv + argcount)) != 64)
						return status;
					exists = 1;
				}
			}
			if (!exists)
				return help(argc, argv);
		}
	}

	run_daemon(argc, argv);
}
