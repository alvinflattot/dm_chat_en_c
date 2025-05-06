/*
 * Serveur de chat multiclient avec gestion de salons dynamiques
 * Fonctionnalités : pseudo unique, messages dans salons, création/entrée de salons, suppression automatique des salons vides
 */

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
#define MAX_SALONS 50
#define MAX_SALON_NAME 32

// Types de rôles
#define ROLE_UTILISATEUR 0
#define ROLE_MODERATEUR 1
#define ROLE_ADMIN 2

// Structure client
typedef struct client {
    int socket;
    char pseudo[MAX_PSEUDO];
    struct salon *salon;
} client_t;

// Structure salon
typedef struct salon {
    char nom[MAX_SALON_NAME];
    client_t *clients[MAX_CLIENTS];
    int nb_clients;
} salon_t;

client_t *clients[MAX_CLIENTS];
salon_t *salons[MAX_SALONS];
int client_count = 0;
int salon_count = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// Salon par défaut (lobby)
salon_t *salon_par_defaut = NULL;

// Recherche un salon par son nom
salon_t *trouver_salon(const char *nom) {
    for (int i = 0; i < salon_count; i++) {
        if (strcmp(salons[i]->nom, nom) == 0) return salons[i];
    }
    return NULL;
}

// Crée un nouveau salon ou retourne celui existant
salon_t *rejoindre_ou_creer_salon(const char *nom) {
    salon_t *s = trouver_salon(nom);
    if (s) return s;

    if (salon_count >= MAX_SALONS) return NULL;
    s = malloc(sizeof(salon_t));
    strncpy(s->nom, nom, MAX_SALON_NAME);
    s->nb_clients = 0;
    salons[salon_count++] = s;
    return s;
}

// Envoie un message à tous les membres du même salon (sauf l'expéditeur si exclus)
void envoyer_message_salon(salon_t *salon, const char *message, int exp_socket, int inclus) {
    for (int i = 0; i < salon->nb_clients; i++) {
        int sock = salon->clients[i]->socket;
        if (inclus || sock != exp_socket) {
            write(sock, message, strlen(message));
        }
    }
}

// Ajoute un client à un salon et notifie les autres membres
void ajouter_client_salon(salon_t *salon, client_t *client) {
    salon->clients[salon->nb_clients++] = client;
    client->salon = salon;

    // Notification de connexion
    char message[MAX_MESSAGE];
    snprintf(message, sizeof(message), "%s s'est connecté.\n", client->pseudo);
    envoyer_message_salon(salon, message, client->socket, 0);
}

// Supprime un client d'un salon et notifie les autres membres
void supprimer_client_salon(client_t *client) {
    salon_t *salon = client->salon;
    if (!salon) return;

    // Notification de déconnexion
    char message[MAX_MESSAGE];
    snprintf(message, sizeof(message), "%s s'est déconnecté.\n", client->pseudo);
    envoyer_message_salon(salon, message, client->socket, 0);

    for (int i = 0; i < salon->nb_clients; i++) {
        if (salon->clients[i] == client) {
            salon->clients[i] = salon->clients[--salon->nb_clients];
            break;
        }
    }

    // Si salon vide (et pas le salon par défaut), le supprimer
    if (salon != salon_par_defaut && salon->nb_clients == 0) {
        for (int i = 0; i < salon_count; i++) {
            if (salons[i] == salon) {
                free(salon);
                salons[i] = salons[--salon_count];
                break;
            }
        }
    }
    client->salon = NULL;
}

// Vérifie l'unicité du pseudo
int pseudo_existe(const char *pseudo) {
    for (int i = 0; i < client_count; ++i) {
        if (strcmp(clients[i]->pseudo, pseudo) == 0) return 1;
    }
    return 0;
}

// Lister les salons existants
void envoyer_liste_salons(int socket) {
    char tampon[MAX_MESSAGE] = "Salons disponibles:\n";
    for (int i = 0; i < salon_count; i++) {
        strcat(tampon, "- ");
        strcat(tampon, salons[i]->nom);
        strcat(tampon, "\n");
    }
    write(socket, tampon, strlen(tampon));
}

// Gère un client (thread)
void *gerer_client(void *arg) {
    int socket = *(int *)arg;
    free(arg);
    char tampon[MAX_MESSAGE];
    int octets;
    char pseudo[MAX_PSEUDO];

    // Demander pseudo unique
    do {
        write(socket, "Entrez votre pseudo: ", 22);
        octets = read(socket, pseudo, MAX_PSEUDO);
        if (octets <= 0) {
            printf(">>> %s s'est déconnecté(e)\n", pseudo);
            close(socket);
            return NULL;
        }
        pseudo[strcspn(pseudo, "\r\n")] = '\0';
        pthread_mutex_lock(&mutex);
        if (pseudo_existe(pseudo)) {
            pthread_mutex_unlock(&mutex);
            write(socket, "Pseudo déjà pris.\n", 21);
        } else {
            pthread_mutex_unlock(&mutex);
            break;
        }
    } while (1);

    // Création client
    client_t *client = malloc(sizeof(client_t));
    client->socket = socket;
    strncpy(client->pseudo, pseudo, MAX_PSEUDO);
    pthread_mutex_lock(&mutex);
    clients[client_count++] = client;
    ajouter_client_salon(salon_par_defaut, client);
    printf(">>> %s s'est connecté(e) et a rejoint le salon %s\n", client->pseudo, client->salon->nom);
    pthread_mutex_unlock(&mutex);

    // Message de bienvenue
    snprintf(tampon, sizeof(tampon), "Bienvenue %s dans %s !\n", pseudo, client->salon->nom);
    write(socket, tampon, strlen(tampon));

    while ((octets = read(socket, tampon, sizeof(tampon) - 1)) > 0) {
        tampon[octets] = '\0';
        tampon[strcspn(tampon, "\r\n")] = '\0';

        if (strcmp(tampon, "/exit") == 0) break;

        pthread_mutex_lock(&mutex);
        if (strcmp(tampon, "/list") == 0) {
            envoyer_liste_salons(socket);
            printf(">>> %s a exécuté la commande /list\n", client->pseudo);
        } else if (strncmp(tampon, "/join ", 6) == 0) {
            char *nom_salon = tampon + 6;
            supprimer_client_salon(client);
            salon_t *s = rejoindre_ou_creer_salon(nom_salon);
            ajouter_client_salon(s, client);
            printf(">>> %s a rejoint le salon %s\n", client->pseudo, client->salon->nom);
            snprintf(tampon, sizeof(tampon), "Vous avez rejoint %s\n", s->nom);
            write(socket, tampon, strlen(tampon));
        } else if (strcmp(tampon, "/number") == 0) {
            int n = rand() % 100;
            snprintf(tampon, sizeof(tampon), "Nombre aléatoire: %d\n", n);
            write(socket, tampon, strlen(tampon));
            printf(">>> %s a exécuté la commande /number\n", client->pseudo);
        } else if (strcmp(tampon, "/date") == 0) {
            time_t now = time(NULL);
            strftime(tampon, sizeof(tampon), "Date serveur : %d/%m/%Y %H:%M:%S\n", localtime(&now));
            write(socket, tampon, strlen(tampon));
            printf(">>> %s a exécuté la commande /date\n", client->pseudo);
        } else {
            char message[MAX_MESSAGE + MAX_PSEUDO + 4];
            snprintf(message, sizeof(message), "%s: %s\n", client->pseudo, tampon);
            printf("[%s][%s] %s\n", client->salon->nom, client->pseudo, tampon);
            envoyer_message_salon(client->salon, message, socket, 1);
        }
        pthread_mutex_unlock(&mutex);
    }

    pthread_mutex_lock(&mutex);
    supprimer_client_salon(client);
    for (int i = 0; i < client_count; i++) {
        if (clients[i] == client) {
            clients[i] = clients[--client_count];
            break;
        }
    }
    pthread_mutex_unlock(&mutex);
    printf(">>> %s s'est déconnecté(e)\n", client->pseudo);
    close(socket);
    free(client);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage : %s port\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    srand(time(NULL));
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

    // Crée le salon par défaut (lobby)
    salon_par_defaut = rejoindre_ou_creer_salon("lobby");

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
