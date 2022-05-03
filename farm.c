/*
 * Progetto del corso di Laboratorio II 2021/22
 *
 * Dipartimento di Informatica Università di Pisa
 *
 * Studente: Yuriy Rymarchuk
 * Matricola: 614484
 * E-mail: yura.ita.com@gmail.com
 */

/**
 * @file farm.c
 * @author Yuriy Rymarchuk
 * @brief File main del progetto
 */

#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <unistd.h>

/*----- Includes Personali -----*/
#include "util.h"
#include "boundedqueue.h"

/*----- DEFINES -----*/
#define EOS (void *)0x1
#define NAME_MAX 256
#define N_THREADS 4L
#define Q_LEN 8L
#define DELAY 0L

#define UNIX_PATH_MAX 108
#define SOCKNAME "./sck_yr "

typedef struct f_struct
{
	char *filename;
	size_t filesize;
} f_struct_t;

/*----- Funzioni -----*/

/**
 * @brief    In caso di chiamata del main con parametri sbagliati, suggerisce come va fatta la chiamata
 *
 * @param    progname nome del programma
 */
static void
format_cmd(const char *progname)
{
	fprintf(stderr, "Il programma va lanciato con il seguente comando:\n");
	fprintf(stderr, "\n\t./%s [OPTION]... [FILES LIST]...\n\n", progname);
	fprintf(stderr, "-n\n    numero di thread (default 4)\n");
	fprintf(stderr, "-q\n    lunghezza delal coda concorrente (default 8)\n");
	fprintf(stderr, "-t\n    tempo in ms tra l'invio delle richieste ai thread Worker (default 0)\n");
	fflush(stdout);
}

/**
 * @brief	Controlla se i valori dei parametri opzionali sono del tipo giusto e nel caso positivo li assegna alla variabile passata
 *
 * @param	val valore da controllare
 * @param	var variabile a cui viene assegnato il valore se passa il controllo
 */
static void
check_param(char *val, long *var)
{
	int r = isNumber(val, var);
	if (r == 1)
	{
		fprintf(stderr, "%s è NaN\n", val);
		fflush(stdout);
		exit(1);
	}
	else if (r == 2)
	{
		fprintf(stderr, "%s ha provocato un overflow/underflow\n", val);
		fflush(stdout);
		exit(2);
	}
}

/**
 * @brief	Fa il test sull'espressione 'test' passata come parametro per determinare se è verificato un errore
 *
 * @param	test condizione che verifica l'errore
 * @param	message messaggio sa stampare nel caso dell'errore
 * @param	__VA_ARGS__
 */
static void
check(int test, const char *message, ...)
{
	if (test)
	{
		va_list args;
		va_start(args, message);
		vfprintf(stderr, message, args);
		va_end(args);
		fprintf(stderr, "\n");
		exit(EXIT_FAILURE);
	}
}

/**
 * @brief	Il corpo del processo Collector
 */
static void Collector(int fd_skt, int fd_c, struct sockaddr_un sa);

/**
 * @brief	Aspetta la terminazione del processo collector e stampa lo status con cui termina il processo
 *
 * @param	pid ID del processo collector
 */
static void
collector_exit_status(pid_t pid)
{
	int status;
	if (waitpid(pid, &status, 0) == -1)
	{
		perror("Waitpid");
		exit(EXIT_FAILURE);
	}

	if (WIFEXITED(status))
	{
		DBG("Processo collector [pid=%d] è terminato con exit(%d)\n", pid, WEXITSTATUS(status));
	}
	else
	{
		DBG("Processo collector [pid=%d] è terminato con un exit irregolare\n", pid);
	}
	fflush(stdout);
}

/**
 * @brief	Start routine dei thread Worker
 */
static void *Worker(void *arg);

/**
 * @brief	Routine per liberare la memoria di elementi f_struct
 */
void F(void *el)
{
	if (el != EOS)
	{
		f_struct_t *f = el;
		free(f->filename);
		free(f);
	}
}

/**
 * @brief	Mappa un file di interi long
 *
 * @param	file_name nome del file
 * @param	contents_ptr puntatore che punta alla locazione di memoria mappata
 * @param	size dimensione della memoria in byte da mappare
 */
void mmap_file(const char *file_name, long **contents_ptr, size_t size);

/*----- MAIN -----*/
int main(int argc, char *argv[])
{
	if (argc == 1)
	{
		format_cmd("farm");
		return 0;
	}

	long n = N_THREADS;
	long q_len = Q_LEN;
	long delay = DELAY;

	int opt;
	while ((opt = getopt(argc, argv, ":n:q:t:")) != -1)
	{
		switch (opt)
		{
		case 'n':
			DBG("Numero di thread: %s\n", optarg);
			check_param(optarg, &n);
			break;
		case 'q':
			DBG("Lunghezza coda: %s\n", optarg);
			check_param(optarg, &q_len);
			break;
		case 't':
			DBG("Delay: %s\n", optarg);
			check_param(optarg, &delay);
			break;
		case ':':
			fprintf(stderr, "opzione %c è stata passata senza un valore\n", opt);
			return 1;
		case '?':
			fprintf(stderr, "opzione sconosciuta: -%c\n", optopt);
			return 1;
			break;
		}
	}

	/*----- SOCKET SETUP -----*/

	int fd_skt, fd_c;
	struct sockaddr_un sa;
	strncpy(sa.sun_path, SOCKNAME, UNIX_PATH_MAX);
	sa.sun_family = AF_UNIX;

	pid_t collector_pid = fork();
	if (collector_pid == 0)
	{
		Collector(fd_skt, fd_c, sa);
		exit(EXIT_SUCCESS);
	}

	/*----- CLIENT SETUP -----*/

	fd_skt = socket(AF_UNIX, SOCK_STREAM, 0);
	while (connect(fd_skt, (struct sockaddr *)&sa, sizeof(sa)) == -1)
	{
		if (errno == ENOENT)
			sleep(1);
		else
			exit(EXIT_FAILURE);
	}

	int N = 100;
	char buf[N];

	write(fd_skt,"Hallo !", 7);
	read(fd_skt, buf, N);
	printf("Client got : %s\n", buf);
	close(fd_skt);

	/*----- MASTER WORKER -----*/

	BQueue_t *q = initBQueue(q_len);

	pthread_t th[n];
	for (size_t i = 0; i < n; i++)
	{
		int r = pthread_create(&th[i], NULL, Worker, q);
		check(r != 0, "pthread_create ha fallito (Worker n.%ld)", i, strerror(r));
	}

	/*----- TEST DEI FILE -----*/

	for (size_t i = optind; i < argc; i++)
	{
		size_t filesize;
		errno = 0;
		if (isRegular(argv[i], &filesize) != 1)
		{
			if (errno == 0)
			{
				fprintf(stderr, "%s non e' un file regolare\n", argv[i]);
				continue;
			}
			perror("isRegular");
			continue;
		}
		f_struct_t *file = malloc(sizeof(f_struct_t));
		file->filename = strdup(argv[i]);
		file->filesize = filesize;

		usleep(delay * 1000);
		push(q, file);
	}
	push(q, EOS);

	for (size_t i = 0; i < n; i++)
	{
		int r = pthread_join(th[i], NULL);
		check(r != 0, "pthread_join ha fallito (Worker n.%ld)\n", i, strerror(r));
	}

	deleteBQueue(q, NULL);
	collector_exit_status(collector_pid);
	return 0;
}

/*----- COLLECTOR -----*/
static void
Collector(int fd_skt, int fd_c, struct sockaddr_un sa)
{
	DBG("Collector is up\n", NULL);

	/*----- SERVER SETUP -----*/
	fd_skt = socket(AF_UNIX, SOCK_STREAM, 0);
	// Fare check di apertura socket (errno)

	bind(fd_skt, (struct sockaddr *)&sa, sizeof(sa));
	// Fare check sul bind (errno)

	listen(fd_skt, SOMAXCONN);
	// Fare check sul return del listen (errno)

	fd_c = accept(fd_skt, NULL, 0);
	// Fare check sul accept (errno)

	int N = 100;
	char buf[N];
	read(fd_c, buf, N);
	printf("Server got : %s\n", buf);
	write(fd_c, "Bye !", 5);
	close(fd_skt);
	close(fd_c);
	exit(EXIT_SUCCESS);
}

static void *Worker(void *arg)
{
	BQueue_t *q = arg;
	// DBG("Start della routine del Worker\n", NULL);
	while (1)
	{
		f_struct_t *f = NULL;
		f = pop(q);
		if (f == EOS)
		{
			break;
		}
		DBG("File ricevuto: %s con dimensione di %ld bytes\n", f->filename, f->filesize);

		/*----- Calcolo di result -----*/

		long *content = NULL;
		mmap_file(f->filename, &content, f->filesize);

		long result = 0;
		for (size_t i = 0; i < f->filesize / 8; i++)
		{
			result += (i * content[i]);
		}

		int len = snprintf(NULL, 0, "%ld\t%s", result, f->filename);
		char res[(len + 1)];
		snprintf(res, (len + 1), "%ld\t%s", result, f->filename);
		printf("%s\n", res);

		munmap(content, f->filesize);
		free(f->filename);
		free(f);
	}
	// DBG("Chiusura del Worker\n", NULL);
	push(q, EOS);
	pthread_exit(NULL);
}

void mmap_file(const char *file_name, long **content_ptr, size_t size)
{
	int fd;
	fd = open(file_name, O_RDONLY);
	check(fd < 0, "Funzione open %s ha fallito: %s", file_name, strerror(errno));
	*content_ptr = mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0);
	check(*content_ptr == MAP_FAILED, "Funzione mmap %s ha fallito: %s", file_name, strerror(errno));
	close(fd);
}