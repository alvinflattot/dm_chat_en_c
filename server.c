//server.c
#include <sys/types.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <time.h>
#include <signal.h> // librairie pour nettoyer les zombies

int nombre_clients = 0;

// Création et configuration de la socket serveur
int socket_serveur(unsigned short port)
{
    int serveur_socket, option = 1;
    struct sockaddr_in serveur_sockaddr_in;

    serveur_socket = socket(PF_INET, SOCK_STREAM, 0); // Socket TCP
    if (serveur_socket == -1)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Permet de réutiliser le port rapidement après un crash
    if (setsockopt(serveur_socket, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) == -1)
    {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    memset(&serveur_sockaddr_in, 0, sizeof(serveur_sockaddr_in));
    serveur_sockaddr_in.sin_family = AF_INET;
    serveur_sockaddr_in.sin_port = htons(port);
    serveur_sockaddr_in.sin_addr.s_addr = INADDR_ANY;  // Accepte toutes les connexions entrantes

    /* Ecoute sur le port mentionne */
    if (bind(serveur_socket, (struct sockaddr *)&serveur_sockaddr_in, sizeof(serveur_sockaddr_in)) == -1)
    {
        perror("bind");
        exit(EXIT_FAILURE);
    }
    if (listen(serveur_socket, SOMAXCONN) == -1)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    return serveur_socket;
}

// Traitement des commandes spéciales du client
void traiter_commande(int client_socket, char *commande) {
    char reponse[BUFSIZ];

    // Supprimer le caractère de nouvelle ligne à la fin
    char *nl = strchr(commande, '\n');
    if (nl) *nl = '\0';

    if (strcmp(commande, "/number") == 0) {
        snprintf(reponse, BUFSIZ, "Nombre de clients connectés: %d\n", nombre_clients);
        write(client_socket, reponse, strlen(reponse));
    }
    else if (strcmp(commande, "/date") == 0) {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);

        strftime(reponse, BUFSIZ, "Date serveur: %d/%m/%Y %H:%M:%S\n", tm_info);
        write(client_socket, reponse, strlen(reponse));
    }
    else if (strcmp(commande, "/exit") == 0) {
        printf(">>> Client déconnecté proprement\n");
        nombre_clients--;
        close(client_socket);
        exit(EXIT_SUCCESS);;
    }
//    else {
//        snprintf(reponse, BUFSIZ, "Commande non reconnue: %s\nCommandes disponibles: /number, /date\n", commande);
//        write(client_socket, reponse, strlen(reponse));
//    }
}

// Fonction de gestion des échanges avec un client
void serveur(int client_socket)
{
    signal(SIGCHLD, SIG_IGN);  // évite les zombies lors de la mort des processus enfants
    while (1)
    {
        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(client_socket, &rset);
        FD_SET(STDIN_FILENO, &rset);

        if (select(client_socket + 1, &rset, NULL, NULL, NULL) == -1)
        {
            perror("select");
            exit(EXIT_FAILURE);
        }

        // Réception de données du client
        if (FD_ISSET(client_socket, &rset))
        {
            int octets;
            unsigned char tampon[BUFSIZ];
            octets = read(client_socket, tampon, sizeof(tampon));
            if (octets <= 0)
            {
                if (octets == 0){
                    printf(">>> Client déconnecté proprement\n");
                }
                else{
                    perror("read");  // // Déconnexion brutale
                }

                nombre_clients--;
                close(client_socket);
                exit(EXIT_SUCCESS);
            }
            write(STDOUT_FILENO, tampon, octets); // Affiche la requête du client
            traiter_commande(client_socket, tampon); // Traitement si commande
        }

        // Saisie standard du serveur (utile pour faire parler le serveur)
        if (FD_ISSET(STDIN_FILENO, &rset))
        {
            int octets;
            unsigned char tampon[BUFSIZ];
            octets = read(STDIN_FILENO, tampon, sizeof(tampon));
            if (octets == 0)
            {
                close(client_socket);
                nombre_clients--;
                exit(EXIT_SUCCESS);
            }
            write(client_socket, tampon, octets);
        }
    }
}

int main(int argc, char **argv)
{
    unsigned short port;
    int serveur_socket;
    if (argc != 2)
    {
        fprintf(stderr, "Erreur sur le nombre d'arguments\nUsage: %s port\n", argv[0]);
        exit(EXIT_FAILURE);
        exit(EXIT_FAILURE);
    }
    port = atoi(argv[1]);
    serveur_socket = socket_serveur(port);
    while (1)
    {
        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(serveur_socket, &rset);
        FD_SET(STDIN_FILENO, &rset);

        if (select(serveur_socket + 1, &rset, NULL, NULL, NULL) - 1)
        {
            perror("select");
            exit(EXIT_FAILURE);
        }

        if (FD_ISSET(serveur_socket, &rset))
        {
            int client_socket;
            struct sockaddr_in client_sockaddr_in;
            socklen_t taille = sizeof(client_sockaddr_in);
            struct hostent *hostent;

            /* En attente de la connexion d'un client (commande bloquante par defaut) */
            client_socket = accept(serveur_socket, (struct sockaddr *)&client_sockaddr_in, &taille);

            if (client_socket == -1)
            {
                perror("accept");
                exit(EXIT_FAILURE);
            }

            nombre_clients++;

            switch (fork())
            {
                case -1: /* erreur */
                {
                    perror("fork");
                    exit(EXIT_FAILURE);
                }
                case 0: /* processus enfant */
                {
                    close(serveur_socket);
                    hostent = gethostbyaddr((char *)&client_sockaddr_in.sin_addr.s_addr, sizeof(client_sockaddr_in.sin_addr.s_addr), AF_INET);
                    if (hostent == NULL)
                    {
                        perror("gethostbyaddr");
                        exit(EXIT_FAILURE);
                    }
                    printf(">>> Connexion depuis %s [%s]\n", hostent->h_name, inet_ntoa(client_sockaddr_in.sin_addr));
                    serveur(client_socket);
                    exit(EXIT_SUCCESS);
                }
                default: /* processus parent */
                {
                    close(client_socket); // Le parent ferme sa copie de la socket
                }
            }
        }
    }
    exit(EXIT_SUCCESS);
}
