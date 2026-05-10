/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_SPAWN_H
#define LABWC_SPAWN_H

#include <sys/types.h>

/**
 * spawn_primary_client - execute asynchronously
 * @command: command to be executed
 */
pid_t spawn_primary_client(const char *command);

/**
 * spawn_async_no_shell - execute asynchronously
 * @command: command to be executed
 */
void spawn_async_no_shell(char const *command);

/**
 * spawn_sync_no_shell - execute synchronously
 * @command: command to be executed
 */
void spawn_sync_no_shell(char const *command);

/**
 * spawn_piped - execute asynchronously
 * @command: command to be executed
 * @pipe_fd: set to the read end of a pipe
 *           connected to stdout of the command
 *
 * Notes:
 * The returned pid_t has to be waited for to
 * not produce zombies and the pipe_fd has to
 * be closed. spawn_piped_close() can be used
 * to ensure both.
 */
pid_t spawn_piped(const char *command, int *pipe_fd);

/**
 * spawn_piped_close - clean up a previous
 *                     spawn_piped() process
 * @pid: will be waitpid()'d for
 * @pipe_fd: will be close()'d
 */
void spawn_piped_close(pid_t pid, int pipe_fd);

/**
 * spawn_write_to_stdin - execute command with data piped to its stdin
 * @command: shell command to be executed
 * @data: data to write to child's stdin
 * @len: length of data in bytes
 *
 * Forks a child process, pipes @data to its stdin, then closes the
 * write end so the child sees EOF. Returns the child PID or -1 on error.
 * The returned pid_t will be reaped by the generic SIGCHLD handler.
 */
pid_t spawn_write_to_stdin(const char *command, const char *data, size_t len);

#endif /* LABWC_SPAWN_H */
