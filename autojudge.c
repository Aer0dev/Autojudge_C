#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>

#define COLOR_ORANGE    "\033[38;2;255;165;0m"
#define COLOR_YELLOW	"\033[38;2;255;255;0m"
#define COLOR_RED	"\033[38;2;255;0;0m"
#define COLOR_GREEN	"\033[38;2;0;255;0m"
#define COLOR_RESET	"\033[0m"
#define MAX_PATH_LENGTH 4096
#define MAX_FILENAME_LENGTH 1024
#define MAX_TEST_INPUTS 20

int total_tests = 0;
int correct_tests = 0;
int timeout_tests = 0;
int runtime_error_tests = 0;
int wrong_answer_tests = 0;
int total_execution_time = 0;


void print_usage() {
    printf("Usage: ./program -i <inputdir> -a <answerdir> -t <timelimit> <target_src>\n");
}

int compile_target(char *target_src) {
    // Compile the target source code
    pid_t pid = fork();
    if (pid == 0) { // Child process
        execlp("gcc", "gcc", "-fsanitize=address", target_src, "-o", "target_program", NULL);
		//printf("now compiling %s\n", target_src); 
        perror("execlp");
        _exit(EXIT_FAILURE);
    }

    else if (pid > 0) {  // Parent process
        int status;
        waitpid(pid, &status, 0);
        return WEXITSTATUS(status); // Return the exit status of the child
    } 
    
    else { // Fork failed
        perror("fork");
        return -1;
    }
}

int run_target(char *target_program, char *input_file, int timelimit) {
    printf("=================================================================\n%s: ", input_file);
    int fd[2];
    if (pipe(fd) == -1) {
        perror("pipe");
        return -1;
    }

    pid_t pid = fork();
    if (pid == 0) { // Child process
        dup2(fd[1], STDOUT_FILENO);
        close(fd[0]);
        close(fd[1]);
        int input_fd = open(input_file, O_RDONLY);
        dup2(input_fd, STDIN_FILENO);
        close(input_fd);

        //signal(SIGALRM, handle_alarm);
        alarm(timelimit);

        // Execute the target program
        execlp(target_program, target_program, NULL);
        char error_message[] = "Error executing target program\n";
        write(STDERR_FILENO, error_message, strlen(error_message));
        exit(EXIT_FAILURE);
    } 
    
    else if (pid > 0) { // Parent process
        close(fd[1]); // Close write end of pipe

        // Set time limit
        struct timeval start, end;
        gettimeofday(&start, NULL);

        int status;
        waitpid(pid, &status, 0);
        if (WEXITSTATUS(status) == 1) {
            printf(COLOR_ORANGE "Runtime Error occurred\n" COLOR_RESET);
            runtime_error_tests++;
        }
        
        if (WIFSIGNALED(status) != 0) {
            int exit_status = WTERMSIG(status);
            if (exit_status == SIGALRM) {
                printf(COLOR_YELLOW "Timeout Occurred\n" COLOR_RESET);
                return 124; // Custom code to indicate a timeout
            }
            printf(COLOR_ORANGE "Runtime Error occurred\n" COLOR_RESET);
            return 125;
        }

        char output_buffer[4096] = {0};
        ssize_t bytes_read = read(fd[0], output_buffer, sizeof(output_buffer));
        if (bytes_read < 0) {
            perror("read");
            return -1;
        }

        gettimeofday(&end, NULL);
        int execution_time = (end.tv_sec - start.tv_sec) * 1000 +
                             (end.tv_usec - start.tv_usec) / 1000;
        total_execution_time += execution_time;
        printf("Execution time: %d ms\n", execution_time);
        
        return WEXITSTATUS(status);
    }
    else { // Fork failed
        perror("fork");
        return -1;
    }
}

int compare_output(char *target_program, char *input_file, char *expected_output) {
    int fd[2];
    if (pipe(fd) == -1) {
        perror("pipe");
        return -1;
    }

    pid_t pid = fork();
    if (pid == 0) { // Child process
        dup2(fd[1], STDOUT_FILENO);
        close(fd[0]);
        close(fd[1]);

        int input_fd = open(input_file, O_RDONLY);
        dup2(input_fd, STDIN_FILENO);
        close(input_fd);

        // Execute the target program
        execlp(target_program, target_program, NULL);
        char error_message[] = "Error executing target program\n";
        write(STDERR_FILENO, error_message, strlen(error_message));
        exit(EXIT_FAILURE);
    } 
    else if (pid > 0) { // Parent process
        close(fd[1]); // Close write end of pipe

        // Read output from child process
        char output_buffer[MAX_FILENAME_LENGTH];
        ssize_t bytes_read = read(fd[0], output_buffer, sizeof(output_buffer));
        close(fd[0]); // Close read end of pipe

        if (bytes_read < 0) {
            perror("read");
            return -1;
        }

        // Compare output with expected output
        FILE *expected_fp = fopen(expected_output, "r");
        if (expected_fp == NULL) {
            perror("fopen");
            return -1; // Unable to open expected output file
        }

        char expected_line[MAX_FILENAME_LENGTH];
        ssize_t bytes_read2 = fread(expected_line, 1,MAX_FILENAME_LENGTH, expected_fp);
        expected_line[bytes_read2] = '\0';
        fclose(expected_fp);

        // Compare output with expected output
        if (strcmp(output_buffer, expected_line) == 0) {
            return 1; // Output matches expected output
        } 
        else {
            return 0; // Output does not match expected output
        }
    }
    else { // Fork failed
        perror("fork");
        return -1;
    }
}



int main(int argc, char *argv[]) {
    // Check if the number of arguments is correct
    if (argc != 8) {
        print_usage();
        return 1;
    }

    char *inputdir = NULL;
    char *answerdir = NULL;
    int timelimit;
    char *target_src = NULL;

    // Parse command line arguments using getopt
    int opt;
    while ((opt = getopt(argc, argv, "i:a:t:")) != -1) {
        switch (opt) {
            case 'i':
                inputdir = optarg;
                break;
            case 'a':
                answerdir = optarg;
                break;
            case 't':
                timelimit = atoi(optarg);
                break;
            default:
                print_usage();
                return 1;
        }
    }

    if (optind < argc) {
        target_src = argv[optind];
    }

    // Check if all required arguments are provided
    if (inputdir == NULL || answerdir == NULL || target_src == NULL) {
        printf("Error: Missing required arguments.\n");
        print_usage();
        return 1;
    }

    // Compile the target program
    int compile_result = compile_target(target_src);
    if (compile_result != 0) {
        printf("Compile Error\n");
        return 1;
    }

    // Process input files
    DIR *dir;
    struct dirent *ent;

    if ((dir = opendir(inputdir)) != NULL) {
        int test_count = 0;
        // iterate through all files in input directory
        while ((ent = readdir(dir)) != NULL && test_count < MAX_TEST_INPUTS) {
            if (ent->d_type == DT_REG) { // if it is a regular file
                char input_file_path[MAX_PATH_LENGTH];
                char answer_file_path[MAX_PATH_LENGTH];
                snprintf(input_file_path, MAX_PATH_LENGTH, "%s/%s", inputdir, ent->d_name);
                snprintf(answer_file_path, MAX_PATH_LENGTH, "%s/%s", answerdir, ent->d_name);

                // Check if corresponding answer file exists
                if (access(answer_file_path, F_OK) != -1) {
                    total_tests++;

                    // Run target program with input file
                    struct timeval start_time, end_time;
                    gettimeofday(&start_time, NULL);
                    int run_result = run_target("./target_program", input_file_path, timelimit);
                    gettimeofday(&end_time, NULL);
                    int execution_time = (end_time.tv_sec - start_time.tv_sec) * 1000 +
                                         (end_time.tv_usec - start_time.tv_usec) / 1000;
                    total_execution_time += execution_time;

                    if (run_result == 0) {
                        // Compare output with answer
                        int compare_result = compare_output("./target_program",input_file_path, answer_file_path);
                        if (compare_result == 1) {
                            printf(COLOR_GREEN"Correct\n"COLOR_RESET);
                            correct_tests++;
                        } 
                        else if (compare_result == 0) {
                            printf(COLOR_RED"Wrong Answer\n"COLOR_RESET);
                            wrong_answer_tests++;
                        }
                    } else if (run_result == 125) {     // Runtime error
                        runtime_error_tests++;
                    } else if (run_result == 124) {     // Timeout
                        timeout_tests++;
                    }
                } 
                else {
                    printf("Error: Corresponding answer file not found for input: %s\n", ent->d_name);
                }
                test_count++;
            }
        }
        closedir(dir);
    }

    else {
        perror("Error: Unable to open input directory");
        return 1;
    }

    printf("=================================================================\n");
    printf("Result\n");
    printf("=================================================================\n");
    printf("Total tests: %d\n", total_tests);
    printf(COLOR_GREEN"Correct Answer: %d\n"COLOR_RESET, correct_tests);
    printf(COLOR_ORANGE"Runtime error: %d\n"COLOR_RESET, runtime_error_tests);
    printf(COLOR_YELLOW"Timeout: %d\n"COLOR_RESET, timeout_tests);
    printf(COLOR_RED"Wrong Answer: %d\n"COLOR_RESET, wrong_answer_tests);
    printf("Total Execution Time: %d ms\n", total_execution_time);
    

    return 0;
}
