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

const size_t MAX_CLIENT_NUMBER = 100;
const size_t BUFFER_SIZE = 1024;
const size_t MAX_FILE_NAME_LENGTH = 256;
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

void* process_client(void* arg)
{
    printf("Thread started\n");
    int* sock_ptr = (int*) arg;
    int sock = *sock_ptr;
    while (1)
    {
      //читаем длину имени файла
        size_t name_len;
        int code;
        code = recv(sock, &name_len, sizeof(name_len), 0);
        if (code <= 0)
        {
            break;
        }
        printf("Receveing...");
        printf("Name length = %d\n", name_len);
      //читаем имя файла
        char* file_name = (char*) malloc(name_len + 1);
        code = recv(sock, file_name, name_len, 0);
        if (code <= 0)
        {
            break;
        }
        file_name[name_len] = '\0';
        printf("File name: %s\n", file_name);
      //читаем длинну содержимого
        size_t content_size;
        code = recv(sock, &content_size, sizeof(content_size), 0);
        if (code <=0)
        {
            break;
        }
        printf("Data size = %d\n", content_size);
      //читаем содержимое файла
        char* buffer = (char*) malloc(content_size + 1);
        code = recv(sock, buffer, content_size, 0);
        if (code <=0)
        {
            break;
        }
        buffer[content_size] = '\0';
        printf("CONTENT:\n%s\n", buffer);
      //создаем файлы
        FILE* file = fopen(file_name, "w");
        fwrite(buffer, 1, content_size, file);

        free(file_name);
        free(buffer);
        fclose(file);
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
      //получим имя файла
        printf("File name: ");
        size_t name_len;
        ssize_t read_code = getline(&file_name, &name_len, stdin);
        if (read_code <= 0)
        {
            break;
        }
        name_len = read_code - 1;
        file_name[name_len] = '\0';
        printf("Name: %s(%u)\n", file_name, name_len);
      //получим данные-содержимое файла
        printf("Print content:\n");
        read_code = fread(buffer, 1, BUFFER_SIZE, stdin);
        buffer[read_code] = '\0';
        size_t buffer_length = read_code;
        printf("Content(%d): \n%s\n",read_code, buffer);
      //формируем пакет
        size_t total_length = sizeof(name_len) + name_len + sizeof(buffer_length) + buffer_length;
        char* pocket = (char*) malloc(total_length);
      //записываем длинну названия файла
        size_t* pointer = (size_t*) pocket;
        *pointer = name_len;
      //записываем название файла
        memcpy(pocket + sizeof(size_t), file_name, name_len);
      //записываем размер содержимого
        pointer = (size_t*) (pocket + sizeof(size_t) + name_len);
        *pointer = buffer_length;
      //записываем содержимое
        memcpy(pocket + sizeof(size_t) * 2 + name_len, buffer, buffer_length);
        send_all_buffer(pocket, write_socket, total_length);
        free(pocket);
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
