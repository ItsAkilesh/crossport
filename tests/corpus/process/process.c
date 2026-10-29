/*
 * File: tests/corpus/process/process.c
 * Purpose: Exercise child processes: posix_spawn with arguments, environment and redirection, waitpid, kill, system and popen
 * Author: Akilesh
 * Date: 2026-10-29
 *
 * The program starts itself as the child (through /proc/self/exe) in one of several modes.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHILD_FLAG "--child"
#define SLEEP_FLAG "--sleep"
#define ENV_NAME "CROSSPORT_PROCESS_TEST"
#define LONG_SLEEP_SECONDS 30
#define EXIT_CHILD 7

extern char **environ;

static int g_failures;

static void expect(int condition, const char *what)
{
    printf("%s: %s\n", what, condition ? "ok" : "FAILED");
    g_failures += !condition;
}

static int run_child(int argc, char **argv)
{
    for (int i = 2; i < argc; i++) {
        printf("arg[%d]=<%s>\n", i - 2, argv[i]);
    }
    const char *value = getenv(ENV_NAME);
    printf("env=%s\n", value != NULL ? value : "(unset)");
    fflush(stdout);
    return EXIT_CHILD;
}

static pid_t spawn_self(char *const argv[], char *const envp[], posix_spawn_file_actions_t *actions)
{
    pid_t pid = 0;
    return posix_spawn(&pid, "/proc/self/exe", actions, NULL, argv, envp) == 0 ? pid : -1;
}

static void test_arguments_and_environment_and_capture(void)
{
    char *argv[] = {"process", CHILD_FLAG, "plain", "with space", "has\"quote", "", "trailing\\", NULL};
    char *envp[] = {ENV_NAME "=passed", NULL};
    int pipe_fds[2];
    posix_spawn_file_actions_t actions;
    char text[512] = {0};
    expect(pipe(pipe_fds) == 0, "pipe for the child's output");
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], 1);
    pid_t pid = spawn_self(argv, envp, &actions);
    posix_spawn_file_actions_destroy(&actions);
    expect(pid > 0, "posix_spawn started the child");
    close(pipe_fds[1]);
    size_t used = 0;
    ssize_t got;
    while ((got = read(pipe_fds[0], text + used, sizeof(text) - used - 1)) > 0) {
        used += (size_t)got;
    }
    close(pipe_fds[0]);
    int status = 0;
    expect(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == EXIT_CHILD, "child exit status");
    printf("raw status %d\n", status);
    printf("--- child output ---\n%s--- end ---\n", text);
}

static void test_redirect_to_file(void)
{
    char *argv[] = {"process", CHILD_FLAG, "to-file", NULL};
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 1, "child-out.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    pid_t pid = spawn_self(argv, environ, &actions);
    posix_spawn_file_actions_destroy(&actions);
    int status = 0;
    expect(pid > 0 && waitpid(pid, &status, 0) == pid && WEXITSTATUS(status) == EXIT_CHILD, "child wrote to a file");
    char text[128] = {0};
    FILE *file = fopen("child-out.txt", "r");
    expect(file != NULL && fread(text, 1, sizeof(text) - 1, file) > 0 && strstr(text, "arg[0]=<to-file>") != NULL,
           "the file holds the child's output");
    if (file != NULL) {
        fclose(file);
    }
}

static void test_wait_and_kill(void)
{
    char seconds[16];
    snprintf(seconds, sizeof(seconds), "%d", LONG_SLEEP_SECONDS);
    char *argv[] = {"process", SLEEP_FLAG, seconds, NULL};
    pid_t pid = spawn_self(argv, environ, NULL);
    int status = 0;
    expect(pid > 0, "sleeping child started");
    expect(waitpid(pid, &status, WNOHANG) == 0, "WNOHANG returns 0 for a running child");
    expect(kill(pid, 0) == 0, "kill with signal 0 finds the child");
    expect(kill(pid, SIGTERM) == 0, "SIGTERM sent");
    expect(waitpid(pid, &status, 0) == pid && WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM, "child ended by SIGTERM");
    expect(waitpid(pid, &status, 0) == -1 && errno == ECHILD, "a reaped child is gone");
}

static void test_system_and_popen(void)
{
    int status = system("exit 3");
    expect(WIFEXITED(status) && WEXITSTATUS(status) == 3, "system returns the exit status");
    FILE *pipe_stream = popen("echo from-popen", "r");
    char line[64] = {0};
    expect(pipe_stream != NULL && fgets(line, sizeof(line), pipe_stream) != NULL, "popen read a line");
    line[strcspn(line, "\r\n")] = '\0';
    printf("popen line: <%s>\n", line);
    expect(pipe_stream != NULL && WEXITSTATUS(pclose(pipe_stream)) == 0, "pclose returns the status");
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], CHILD_FLAG) == 0) {
        return run_child(argc, argv);
    }
    if (argc > 2 && strcmp(argv[1], SLEEP_FLAG) == 0) {
        sleep((unsigned)atoi(argv[2]));
        return 0;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    test_arguments_and_environment_and_capture();
    test_redirect_to_file();
    test_wait_and_kill();
    test_system_and_popen();
    printf("failures: %d\n", g_failures);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
