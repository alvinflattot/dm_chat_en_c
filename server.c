// server.c (modifié pour éviter la troncation avec calcul de longueur)
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

#define MAX_CLIENTS 100
#define MAX_MSG_LEN 8192 // Limite de taille de message

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s port\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    unsigned short port = atoi(argv[1]);
    int serveur_socket, client_socket;
    struct sockaddr_in serveur_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    int clients[MAX_CLIENTS];
    char *pseudos[MAX_CLIENTS]; // Tableau pour stocker les pseudos
    int max_sd, sd, activity, i, new_socket;
    fd_set readfds;

    // Init clients
    for (i = 0; i < MAX_CLIENTS; i++)
        clients[i] = 0;

    // Création de la socket serveur
    serveur_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (serveur_socket == 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(serveur_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0)
    {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    serveur_addr.sin_family = AF_INET;
    serveur_addr.sin_addr.s_addr = INADDR_ANY;
    serveur_addr.sin_port = htons(port);

    if (bind(serveur_socket, (struct sockaddr *)&serveur_addr, sizeof(serveur_addr)) < 0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    printf(">>> Serveur lancé sur le port %d\n", port);

    if (listen(serveur_socket, 3) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    while (1)
    {
        // Vider et préparer les sockets à écouter
        FD_ZERO(&readfds);
        FD_SET(serveur_socket, &readfds);
        max_sd = serveur_socket;

        // Ajouter les sockets clients
        for (i = 0; i < MAX_CLIENTS; i++)
        {
            sd = clients[i];
            if (sd > 0)
                FD_SET(sd, &readfds);
            if (sd > max_sd)
                max_sd = sd;
        }

        // Attendre activité
        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0)
        {
            perror("select error");
            continue;
        }

        // Nouvelle connexion
        if (FD_ISSET(serveur_socket, &readfds))
        {
            new_socket = accept(serveur_socket, (struct sockaddr *)&client_addr, &client_len);
            if (new_socket < 0)
            {
                perror("accept");
                exit(EXIT_FAILURE);
            }

            // Attribuer un pseudo par défaut
            char pseudo[50] = "Client";
            snprintf(pseudo, sizeof(pseudo), "Client %d", new_socket);

            // Ajouter le client à la liste
            for (i = 0; i < MAX_CLIENTS; i++)
            {
                if (clients[i] == 0)
                {
                    clients[i] = new_socket;
                    pseudos[i] = strdup(pseudo); // Enregistrer le pseudo
                    break;
                }
            }

            printf(">>> Nouveau client connecté: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            printf(">>> Pseudo attribué: %s\n", pseudo);
        }

        // Lire les messages
        for (i = 0; i < MAX_CLIENTS; i++)
        {
            sd = clients[i];

            if (FD_ISSET(sd, &readfds))
            {
                char buffer[BUFSIZ];
                int valread = read(sd, buffer, sizeof(buffer));

                // Déconnexion
                if (valread == 0)
                {
                    getpeername(sd, (struct sockaddr *)&client_addr, &client_len);
                    printf(">>> Client déconnecté: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                    close(sd);
                    clients[i] = 0;
                    free(pseudos[i]); // Libérer le pseudo
                }
                else
                {
                    // Ajout du pseudo devant le message
                    buffer[valread] = '\0'; // Assurez-vous de finir la chaîne
                    char message[MAX_MSG_LEN];
                    int message_len = snprintf(message, sizeof(message), "%s: %s", pseudos[i], buffer);

                    // Vérification de la longueur du message et troncature si nécessaire
                    if (message_len >= MAX_MSG_LEN)
                    {
                        fprintf(stderr, "Message trop long, coupé.\n");
                        message[MAX_MSG_LEN - 1] = '\0'; // Assurer la troncature
                    }

                    // Envoyer aux autres clients
                    for (int j = 0; j < MAX_CLIENTS; j++)
                    {
                        if (clients[j] != 0 && clients[j] != sd)
                        {
                            write(clients[j], message, strlen(message));
                        }
                    }
                }
            }
        }
    }

    return 0;
}
