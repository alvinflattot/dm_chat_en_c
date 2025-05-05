#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include <time.h>

#define MAX_CLIENTS 100
#define MAX_PSEUDO 32

typedef struct {
    int socket;
    char pseudo[MAX_PSEUDO];
} client_t;

client_t clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

// Vérifie si un pseudo est déjà pris
int pseudo_existe(const char *pseudo) {
    for (int i = 0; i < client_count; ++i) {
        if (strcmp(clients[i].pseudo, pseudo) == 0) {
            return 1;
        }
    }
    return 0;
}

// Ajoute un client (protégé par mutex)
void ajouter_client(int socket, const char *pseudo) {
    pthread_mutex_lock(&clients_mutex);
    if (client_count < MAX_CLIENTS) {
        clients[client_count].socket = socket;
        strncpy(clients[client_count].pseudo, pseudo, MAX_PSEUDO - 1);
        clients[client_count].pseudo[MAX_PSEUDO - 1] = '\0';
        client_count++;
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Supprime un client (protégé par mutex)
void supprimer_client(int socket) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; ++i) {
        if (clients[i].socket == socket) {
            clients[i] = clients[client_count - 1]; // Remplace par le dernier
            client_count--;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void *gerer_client(void *arg) {
    int socket = *(int *)arg;
    free(arg);

    char tampon[BUFSIZ];
    int octets;

    // Demande du pseudo
    char pseudo[MAX_PSEUDO];
    do {
        write(socket, "Entrez votre pseudo: ", 22);
        octets = read(socket, pseudo, MAX_PSEUDO);
        if (octets <= 0) {
            close(socket);
            return NULL;
        }
        pseudo[strcspn(pseudo, "\r\n")] = '\0';
    } while (pseudo_existe(pseudo));

    ajouter_client(socket, pseudo);
    printf(">>> %s connecté\n", pseudo);

    // Boucle principale
    while ((octets = read(socket, tampon, sizeof(tampon))) > 0) {
        tampon[octets] = '\0';

        // Traitement des commandes
        if (strncmp(tampon, "/number", 7) == 0) {
            char reponse[128];
            snprintf(reponse, sizeof(reponse), "Clients connectés : %d\n", client_count);
            write(socket, reponse, strlen(reponse));
        }
        else if (strncmp(tampon, "/date", 5) == 0) {
            time_t now = time(NULL);
            char reponse[128];
            strftime(reponse, sizeof(reponse), "Date serveur : %d/%m/%Y %H:%M:%S\n", localtime(&now));
            write(socket, reponse, strlen(reponse));
        }
        else if (strncmp(tampon, "/exit", 5) == 0) {
            break;
        }
        else {
            // Echo
            write(socket, tampon, octets);
        }
    }

    printf(">>> %s déconnecté\n", pseudo);
    supprimer_client(socket);
    close(socket);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage : %s port\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int serveur_socket;
    unsigned short port = atoi(argv[1]);
    struct sockaddr_in serveur_addr;

    serveur_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (serveur_socket == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(serveur_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&serveur_addr, 0, sizeof(serveur_addr));
    serveur_addr.sin_family = AF_INET;
    serveur_addr.sin_port = htons(port);
    serveur_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serveur_socket, (struct sockaddr *)&serveur_addr, sizeof(serveur_addr)) == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(serveur_socket, 10) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf(">>> Serveur en attente sur le port %d...\n", port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int *client_socket = malloc(sizeof(int));
        *client_socket = accept(serveur_socket, (struct sockaddr *)&client_addr, &len);
        if (*client_socket == -1) {
            perror("accept");
            free(client_socket);
            continue;
        }

        pthread_t tid;
        pthread_create(&tid, NULL, gerer_client, client_socket);
        pthread_detach(tid);
    }

    close(serveur_socket);
    return 0;
}
