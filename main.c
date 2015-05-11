#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>

const size_t MAX_CLIENT_NUMBER = 100;
const size_t BUFFER_SIZE = 1024;
const size_t POCKET_SIZE = 1024 * 1024;
const size_t MAX_FILE_NAME_LENGTH = 256;
const int CMD_PUSH_FILE = 0;
const int CMD_PULL_FILE = 1;
const int SERVER_ANSWER_FILE_EXIST = 1; 
const int SERVER_ANSWER_FILE_NOT_EXIST = 0;
const int IS_FREE = 0;

int init_socket_for_connection(int port)
{
    int listen_socket = -1;
  //создаем сокет
    listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket == -1)
    {
        perror("Can't create socket.");
        exit(EXIT_FAILURE);
    }
  //связываение сокета
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listen_socket, (struct sockaddr *) &addr, sizeof(addr)) == -1)
    {
        perror("Bind");
        exit(EXIT_FAILURE);
    }
    return listen_socket;
}

int send_all_buffer(char* text, int fd, size_t length)
{
    size_t current = 0;
    while (current < length)
    {
        int code = send(fd, text + current, length - current, 0);
        if (code == -1)
        {
            return current;
        }
        current += code;
    }
    return current;
}

int receive_all_buffer(char* text, int fd, size_t length)
{
    size_t current = 0;
    while (current < length)
    {
        int code = recv(fd, text + current, length - current, 0);
        //printf("%d, %d\n", code, current);
        if (code == -1)
        {
            return -1;
        }
        current += code;
    }
}

int send_pocket_file(int sock, char* file_name)
{
  //получаем размер файла  
    struct stat info;
    if (stat(file_name, &info) == -1)
    {
        perror("Can't open file");
        return -1;
    }
    size_t file_length = info.st_size;
  //открываем файл  
    int file_d = open(file_name, O_RDONLY);
    if (file_d == -1)
    {
        perror("Can't open file");
        return -1;
    }
    size_t name_len = strlen(file_name);
    send_all_buffer(&name_len, sock, sizeof(name_len));
    send_all_buffer(file_name, sock, name_len);
    send_all_buffer(&file_length, sock, sizeof(file_length));
    size_t sended = 0;
    char* text = (char*) malloc(POCKET_SIZE);
    while (sended < file_length)
    {
        size_t to_send;
        if (file_length - sended >= POCKET_SIZE)
        {
            to_send = POCKET_SIZE;
        }
        else
        {
            to_send = file_length - sended;
        }
        read(file_d, text, to_send);
      //отправляем пакет  
        send_all_buffer(text, sock, to_send);
        sended += to_send;
    }
    free(text);
    printf("%d\n", sended);
    return 0;
}

int receive_pocket_file(int sock)
{
  //читаем длину имени файла
    size_t name_len;
    int code;
    code = recv(sock, &name_len, sizeof(name_len), 0);
    if (code <= 0)
    {
        return -1;
    }
    printf("Name length = %d\n", name_len);
  //читаем имя файла
    char* file_name = (char*) malloc(name_len + 1);
    code = recv(sock, file_name, name_len, 0);
    if (code <= 0)
    {
        free(file_name);
        return -1;
    }
    file_name[name_len] = '\0';
    printf("File name: %s\n", file_name);
  //читаем длинну содержимого
    size_t content_size;
    code = recv(sock, &content_size, sizeof(content_size), 0);
    if (code <= 0)
    {
        free(file_name);
        return -1;
    }
    printf("Data size = %d\n", content_size);
    char* buffer = (char*) malloc(POCKET_SIZE);
    FILE* file = fopen(file_name, "w");
  //будем отправлять весь файл кусками по POCKET_SIZE байт  
    size_t received = 0;
    while (received < content_size)
    {
        size_t to_recv;

        if (content_size - received >= POCKET_SIZE)
        {
            to_recv = POCKET_SIZE;
        }
        else
        {
            to_recv = content_size - received;
        }
      //отправляем пакет  
        if (receive_all_buffer(buffer, sock, to_recv) == -1)
        {
            break;
        }
        received += to_recv;
        fwrite(buffer, 1, to_recv, file);
    }
    printf("end of read\n");
    free(file_name);
    free(buffer);
    fclose(file);
}

void* process_client(void* arg)
{
    printf("Thread started\n");
    int* sock_ptr = (int*) arg;
    int sock = *sock_ptr;
    while (1)
    {
        int comand;
        if (recv(sock, &comand, sizeof(comand), 0) <= 0)
        {
            break;
        }
        printf("Comand: %d\n", comand);
        if (comand == CMD_PUSH_FILE)
        {
            printf("Receiving...\n");
            if (receive_pocket_file(sock) == -1)
            {
                break;
            }
            continue;
        }
        
        if (comand == CMD_PULL_FILE)
        {
            size_t name_len;
            receive_all_buffer(&name_len, sock, sizeof(name_len));
            char* file_name = (char*) malloc(name_len + 1);
            file_name[name_len] = '\0';
            receive_all_buffer(file_name, sock, name_len);
            printf("Try to send file %s...\n", file_name);
            if (access(file_name, R_OK | F_OK) != -1)
            {
                printf("Success, sending...\n");
              //отсылаем код результата - файл существует
                send_all_buffer(&SERVER_ANSWER_FILE_EXIST, sock, sizeof(SERVER_ANSWER_FILE_EXIST));
                send_pocket_file(sock, file_name);
            }
            else
            {
                perror("Can't open file.");
              //отсылаем код результата - файл НЕ существует
                send_all_buffer(&SERVER_ANSWER_FILE_NOT_EXIST, sock, sizeof(SERVER_ANSWER_FILE_EXIST));
            }
            free(file_name);
            continue;
        }
        printf("Unknown operation code. Aborting...\n");
        exit(EXIT_FAILURE);
    }
    printf("Disconnected.\n");
    *sock_ptr = IS_FREE;
    return NULL;
}

void run_server(int port)
{
    printf("Server started. Port is %d!\n", port);
    int listen_socket = init_socket_for_connection(port);

    int active_connections[MAX_CLIENT_NUMBER];
    pthread_t client_threads[MAX_CLIENT_NUMBER];
    memset(active_connections, 0, MAX_CLIENT_NUMBER * sizeof(int));

    if (listen(listen_socket, 10) == -1)
    {
        perror("Listen");
        exit(EXIT_FAILURE);
    }

    while(1)
    {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(0, &read_set);
        FD_SET(listen_socket, &read_set);
      //ожидаем событий ввода  
        int code = select(listen_socket + 1, &read_set, NULL, NULL, NULL);
        if (code == -1)
        {
            perror("Select error.");
            exit(EXIT_FAILURE);
        }
      //(1)получили запрос на подключение
        if (FD_ISSET(listen_socket, &read_set))
        {
            int connector_socket = accept(listen_socket, NULL, NULL);
            if (connector_socket == -1)
            {
                perror("Accept");
            }
            else
            {
                printf("New client connected!\n");
              //добавляем нового клиента в список
                ssize_t new_client_index = -1;
                for (size_t i = 0; i < MAX_CLIENT_NUMBER; ++i)
                {
                    if (active_connections[i] == IS_FREE)
                    {
                        active_connections[i] = connector_socket;
                        new_client_index = i;
                        break;
                    }
                }
                if (new_client_index == -1)
                {
                    fprintf(stderr, "Too many clients.\n");
                    exit(EXIT_FAILURE);
                }
              //запускаем обработку запросов клиента
                if (pthread_create(client_threads + new_client_index, NULL, &process_client,
                    active_connections + new_client_index) == -1)
                {
                    perror("pthread_create");
                    exit(EXIT_FAILURE);
                }
            }
        }
      //(2) нажатие на клавиатуре  
        if (FD_ISSET(0, &read_set))
        {
            printf("Terminateing...\n");
            for (size_t i = 0; i < MAX_CLIENT_NUMBER; ++i)
            {
                if (active_connections[i] != IS_FREE)
                {
                    pthread_join(client_threads[i], NULL);
                }
            }
            break;
        }
    }
    close(listen_socket);
    printf("Server terminated.\n");
}

void run_client(int port, char* server_addr)
{
    printf("Welcome!\n");
  //создаем сокет
    int write_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(write_socket < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }
  //подключаемся к серверу
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(server_addr);
    if(connect(write_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("Connect");
        exit(EXIT_FAILURE);
    }
    char buffer[BUFFER_SIZE];
    char* file_name = (char*) malloc(MAX_FILE_NAME_LENGTH);

    while (1)
    {
        printf("Command: ");
      //создаем множество для мультиплексировния stdin и socket'a
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(0, &read_set);
        FD_SET(write_socket, &read_set);
      //ожидаем событий ввода  
        int code = select(write_socket + 1, &read_set, NULL, NULL, NULL);
        if (code == -1)
        {
            perror("Select error.");
            free(file_name);
            exit(EXIT_FAILURE);
        }
      //(1) ввод с клавиатуры
        if (FD_ISSET(0, &read_set))
        {
          //получим имя файла
            size_t name_len;
            char comand[100];
            ssize_t read_code = scanf("%s %s", comand, file_name);
            if (read_code <= 0)
            {
                break;
            }
            name_len = strlen(file_name);
            printf("Name: %s(%u)\n", file_name, name_len);
            if (strcmp("push", comand) == 0)
            {
                printf("Sending...\n");
              //отсылаем код команды  
                send_all_buffer(&CMD_PUSH_FILE, write_socket, sizeof(CMD_PUSH_FILE));
              //отсылаем основной пакет  
                send_pocket_file(write_socket, file_name);
                continue;
            }
            if (strcmp("pull", comand) == 0)
            {
                printf("Try to pull file %s\n", file_name);
                size_t name_len = strlen(file_name);
                send_all_buffer(&CMD_PULL_FILE, write_socket, sizeof(CMD_PUSH_FILE));
                send_all_buffer(&name_len, write_socket, sizeof(name_len));
                send_all_buffer(file_name, write_socket, name_len);
                //send_pocket_file(write_socket, file_name);
                continue;
            }
            printf("Use push <filename> to send file to server. Use pull <filename> to get file from server.\n");
        }
      //(2) сервер прислал ответ на запрос
        if (FD_ISSET(write_socket, &read_set))
        {
            int answer;
            receive_all_buffer(&answer, write_socket, sizeof(answer));
            if (answer == SERVER_ANSWER_FILE_EXIST)
            {
                printf("Success!\n");
                receive_pocket_file(write_socket);
                continue;
            }
            else
            {
                printf("File not exist\n");
                continue;
            }
            printf("Unknown server answer code. Aborting...\n");
            exit(EXIT_FAILURE);
        }
    }
    free(file_name);
    printf("Goodbye!\n");
}

void read_args(int argc, char** argv, int* mode, char** addr, int* port)
{
    if (argc < 3)
    {
        fprintf(stderr, "Wrong arguments.\n");
        exit(EXIT_FAILURE);
    }
    *mode = -1;
    if (strcmp(argv[1], "-s") == 0)
    {
        *mode = 0;
        if (argc != 3)
        {
            fprintf(stderr, "Wrong arguments.\n");
            exit(EXIT_FAILURE);
        }
        sscanf(argv[2], "%d", port);
        return;
    }
    if (strcmp(argv[1], "-c") == 0)
    {
        *mode = 1;
        if (argc != 4)
        {
            fprintf(stderr, "Wrong arguments.\n");
            exit(EXIT_FAILURE);
        }
        sscanf(argv[2], "%d", port);
        *addr = argv[3];
        return;
    }
    fprintf(stderr, "Wrong mode. Use -s to run server or -c  to run client.\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv)
{
    int mode, port;
    char* addr;
    read_args(argc, argv, &mode, &addr, &port);
    if (mode == 0)
    {
        run_server(port);
    }
    if (mode == 1)
    {
        run_client(port, addr);
    }
    return EXIT_SUCCESS;
}
