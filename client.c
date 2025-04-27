// client.c
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

// Création et connexion du client à un serveur donné (adresse + port)
int socket_client(char *serveur, unsigned short port)
{
    int client_socket;
    struct hostent *hostent;
    struct sockaddr_in serveur_sockaddr_in;

    /* Creation de la socket */
    client_socket = socket(PF_INET, SOCK_STREAM, 0);
    if (client_socket == -1)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    if (inet_addr(serveur) == INADDR_NONE) /* nom */
    {
        hostent = gethostbyname(serveur);
        if (hostent == NULL)
        {
            perror("gethostbyname");
            exit(EXIT_FAILURE);
        }
    }
    else /* adresse IP */
    {
        unsigned long addr = inet_addr(serveur);
        hostent = gethostbyaddr((char *)&addr, sizeof(addr), AF_INET);
        if (hostent == NULL)
        {
            perror("gethostbyaddr");
            exit(EXIT_FAILURE);
        }
    }

    memset(&serveur_sockaddr_in, 0, sizeof(serveur_sockaddr_in));
    serveur_sockaddr_in.sin_family = AF_INET;
    serveur_sockaddr_in.sin_port = htons(port);

    memcpy(&serveur_sockaddr_in.sin_addr.s_addr, hostent->h_addr_list[0], hostent->h_length);

    printf(">>> Connexion vers le port %d de %s [%s]\n", port, hostent->h_name, inet_ntoa(serveur_sockaddr_in.sin_addr));
    if (connect(client_socket, (struct sockaddr *)&serveur_sockaddr_in, sizeof(serveur_sockaddr_in)) == -1)
    {
        perror("connect");
        exit(EXIT_FAILURE);
    }
    return client_socket;
}

int main(int argc, char **argv)
{
    char *serveur;
    unsigned short port;
    int client_socket;

    if (argc != 3)
    {
        fprintf(stderr, "Erreur sur le nombre d'arguments\nUsage: %s serveur port\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    serveur = argv[1];
    port = atoi(argv[2]);
    client_socket = socket_client(serveur, port);

    for (;;)
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

        // Message du serveur
        if (FD_ISSET(client_socket, &rset))
        {
            int octets;
            unsigned char tampon[BUFSIZ];
            octets = read(client_socket, tampon, sizeof(tampon));

            if (octets <= 0)
            {
                if (octets == 0)
                  fprintf(stderr, ">>> Serveur déconnecté proprement\n");
                else
                  perror("read");

                fprintf(stderr, ">>> Déconnexion du client\n");
                close(client_socket);
                exit(EXIT_FAILURE);
            }

            write(STDOUT_FILENO, tampon, octets);
        }

        // Saisie utilisateur (envoyée au serveur)
        if (FD_ISSET(STDIN_FILENO, &rset))
        {
            int octets;
            unsigned char tampon[BUFSIZ];
            octets = read(STDIN_FILENO, tampon, sizeof(tampon));
            if (octets == 0)
            {
                close(client_socket);
                exit(EXIT_SUCCESS);
            }
            write(client_socket, tampon, octets);
        }
    }

    exit(EXIT_SUCCESS);
}
