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
#define MAX_MESSAGE 1024

typedef struct {
    int socket;
    char pseudo[MAX_PSEUDO];
} client_t;

client_t clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

// Vérifie si un pseudo est déjà utilisé
int pseudo_existe(const char *pseudo) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; ++i) {
        if (strcmp(clients[i].pseudo, pseudo) == 0) {
            pthread_mutex_unlock(&clients_mutex);
            return 1;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return 0;
}

// Ajoute un client à la liste
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

// Supprime un client de la liste
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

// Envoie un message à tous les clients sauf l'expéditeur
void envoyer_message_tous(const char *message, int exp_socket) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; ++i) {
        if (clients[i].socket != exp_socket) {
            write(clients[i].socket, message, strlen(message));
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void *gerer_client(void *arg) {
    int socket = *(int *)arg;
    free(arg);

    char tampon[MAX_MESSAGE];
    int octets;
    char pseudo[MAX_PSEUDO];

    // Demande du pseudo unique
    do {
        write(socket, "Entrez votre pseudo: ", 22);
        octets = read(socket, pseudo, MAX_PSEUDO);
        if (octets <= 0) {
            close(socket);
            return NULL;
        }
        pseudo[strcspn(pseudo, "\r\n")] = '\0';
        if (pseudo_existe(pseudo)) {
            write(socket, "Pseudo déjà pris. Essayez un autre.\n", 37);
        }
    } while (pseudo_existe(pseudo));

    ajouter_client(socket, pseudo);
    printf(">>> %s connecté\n", pseudo);

    // Envoie un message aux autres pour indiquer la connexion
    char notif_connexion[MAX_MESSAGE];
    snprintf(notif_connexion, sizeof(notif_connexion), "*** %s a rejoint le chat ***\n", pseudo);
    envoyer_message_tous(notif_connexion, socket);

    // Message de bienvenue personnel
    char bienvenue[MAX_MESSAGE];
    snprintf(bienvenue, sizeof(bienvenue), "Bienvenue %s ! Tapez /exit pour quitter.\n", pseudo);
    write(socket, bienvenue, strlen(bienvenue));

    while ((octets = read(socket, tampon, sizeof(tampon) - 1)) > 0) {
        tampon[octets] = '\0';
        tampon[strcspn(tampon, "\r\n")] = '\0';

        if (strcmp(tampon, "/number") == 0) {
            int n = rand() % 100;
            char msg[64];
            snprintf(msg, sizeof(msg), "Nombre aléatoire: %d\n", n);
            write(socket, msg, strlen(msg));
            continue;
        } else if (strcmp(tampon, "/date") == 0) {
            time_t now = time(NULL);
            char msg[128];
            strftime(msg, sizeof(msg), "Date serveur : %d/%m/%Y %H:%M:%S\n", localtime(&now));
            write(socket, msg, strlen(msg));
            continue;
        } else if (strcmp(tampon, "/exit") == 0) {
            break;
        }

        char message[MAX_MESSAGE + MAX_PSEUDO + 4];
        snprintf(message, sizeof(message), "%s: %s\n", pseudo, tampon);

        printf("%s", message);
        envoyer_message_tous(message, socket);
    }

    // Notifie les autres de la déconnexion
    printf(">>> %s déconnecté\n", pseudo);
    char notif_depart[MAX_MESSAGE];
    snprintf(notif_depart, sizeof(notif_depart), "*** %s a quitté le chat ***\n", pseudo);
    envoyer_message_tous(notif_depart, socket);

    supprimer_client(socket);
    close(socket);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage : %s port\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    unsigned short port = atoi(argv[1]);
    int serveur_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (serveur_socket == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(serveur_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serveur_addr;
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

    printf(">>> Serveur en écoute sur le port %d...\n", port);

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
        pthread_detach(tid); // Pas besoin de join
    }

    close(serveur_socket);
    return 0;
}
