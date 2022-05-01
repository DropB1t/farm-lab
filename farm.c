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
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/*----- Includes Personali -----*/
#include "util.h"
#include "boundedqueue.h"

/*----- DEFINES -----*/
#define NAME_MAX 256
#define N_THREADS 4L
#define Q_LEN 8L
#define DELAY 0L

/*----- Funzioni -----*/

/**
 * @brief    In caso di chiamata del main con parametri sbagliati, suggerisce come va fatta la chiamata
 *
 * @param    progname nome del programma
 */
void format_cmd(const char *progname)
{
	fprintf(stderr, "Il programma va lanciato con il seguente comando:\n");
	fprintf(stderr, "\n\t./%s [OPTION]... [FILES LIST]...\n\n", progname);
	fprintf(stderr, "-n\n    numero di thread (default 4)\n");
	fprintf(stderr, "-q\n    lunghezza delal coda concorrente (default 8)\n");
	fprintf(stderr, "-t\n    tempo in ms tra l'invio delle richieste ai thread Worker (default 0)\n");
}

/**
 * @brief	Controlla se i valori dei parametri opzionali sono del tipo giusto e nel caso positivo li assegna alla variabile passata
 *
 * @param	val valore da controllare
 * @param	var variabile a cui viene assegnato il valore se passa il controllo
 */
void check_param(char *val, long *var)
{
	int r = isNumber(val, var);
	if (r == 1)
	{
		fprintf(stderr, "%s è NaN\n", val);
		exit(1);
	}
	else if (r == 2)
	{
		fprintf(stderr, "%s ha provocato un overflow/underflow\n", val);
		exit(2);
	}
}

/**
 * @brief	Il corpo del processo Collector
 */
void Collector();

/**
 * @brief	Aspetta la terminazione del processo collector e stampa lo status con cui termina il processo
 *
 * @param	pid ID del processo collector
 */
void collector_exit_status(pid_t pid)
{
	int status;
	if (waitpid(pid, &status, 0) == -1)
	{
		perror("Waitpid");
		exit(EXIT_FAILURE);
	}

	if (WIFEXITED(status))
	{
		fprintf(stdout, "Processo collector [pid=%d] è terminato con exit(%d)\n", pid, WEXITSTATUS(status));
	}
	else
	{
		fprintf(stdout, "Processo collector [pid=%d] è terminato con un exit irregolare\n", pid);
	}
	fflush(stdout);
}

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

	pid_t collector_pid = fork();
	if (collector_pid == 0)
	{
		Collector();
		exit(EXIT_SUCCESS);
	}

	collector_exit_status(collector_pid);
	return 0;
}

void Collector()
{
	printf("Collector is up\n");
	sleep(2);
}