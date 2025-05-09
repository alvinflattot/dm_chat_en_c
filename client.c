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

// Taille maximale du tampon de lecture/écriture
#define TAILLE_TAMPON BUFSIZ

/**
 * Crée et connecte un client à un serveur donné (nom ou adresse IP) et port spécifié.
 * @param nom_serveur Le nom d'hôte ou l'adresse IP du serveur
 * @param port_serveur Le port TCP sur lequel se connecter
 * @return Le descripteur de la socket connectée au serveur
 */
int creer_socket_client(const char *nom_serveur, unsigned short port_serveur)
{
    int descripteur_client;
    struct hostent *infos_hote;
    struct sockaddr_in adresse_serveur;

    // Création de la socket TCP
    descripteur_client = socket(PF_INET, SOCK_STREAM, 0);
    if (descripteur_client == -1) {
        perror("Échec de la création de la socket");
        exit(EXIT_FAILURE);
    }

    // Résolution du nom d'hôte si nécessaire
    if (inet_addr(nom_serveur) == INADDR_NONE) {
        infos_hote = gethostbyname(nom_serveur);
        if (infos_hote == NULL) {
            perror("Impossible de résoudre le nom d'hôte");
            close(descripteur_client);
            exit(EXIT_FAILURE);
        }
    } else {
        // L'utilisateur a fourni une adresse IP directement
        unsigned long adresse = inet_addr(nom_serveur);
        infos_hote = gethostbyaddr((char *)&adresse, sizeof(adresse), AF_INET);
        if (infos_hote == NULL) {
            perror("Impossible de résoudre l'adresse IP");
            close(descripteur_client);
            exit(EXIT_FAILURE);
        }
    }

    // Préparation de la structure d'adresse du serveur
    memset(&adresse_serveur, 0, sizeof(adresse_serveur));
    adresse_serveur.sin_family = AF_INET;
    adresse_serveur.sin_port = htons(port_serveur);
    memcpy(&adresse_serveur.sin_addr.s_addr, infos_hote->h_addr_list[0], infos_hote->h_length);

    // Affichage de l'information de connexion
    printf(">>> Connexion à %s (%s) sur le port %d\n", infos_hote->h_name, inet_ntoa(adresse_serveur.sin_addr), port_serveur);

    // Tentative de connexion
    if (connect(descripteur_client, (struct sockaddr *)&adresse_serveur, sizeof(adresse_serveur)) == -1) {
        perror("Échec de la connexion");
        close(descripteur_client);
        exit(EXIT_FAILURE);
    }

    return descripteur_client;
}

int main(int argc, char **argv)
{
    const char *nom_serveur;
    unsigned short port_serveur;
    int descripteur_client;

    if (argc != 3) {
        fprintf(stderr, "Usage : %s <serveur> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    nom_serveur = argv[1];
    port_serveur = (unsigned short)atoi(argv[2]);

    // Création et connexion du client
    descripteur_client = creer_socket_client(nom_serveur, port_serveur);

    // Boucle principale : lecture via select() sur stdin et socket
    for (;;) {
        fd_set ensemble_descripteurs;
        FD_ZERO(&ensemble_descripteurs);
        FD_SET(descripteur_client, &ensemble_descripteurs);
        FD_SET(STDIN_FILENO, &ensemble_descripteurs);

        // Attente bloquante d'une activité
        if (select(descripteur_client + 1, &ensemble_descripteurs, NULL, NULL, NULL) == -1)
        {
            perror("select");
            close(descripteur_client);
            exit(EXIT_FAILURE);
        }

        // Données du serveur disponibles ?
        if (FD_ISSET(descripteur_client, &ensemble_descripteurs))
        {
            ssize_t nb_octets;
            unsigned char tampon[TAILLE_TAMPON];

            nb_octets = read(descripteur_client, tampon, sizeof(tampon));
            if (nb_octets <= 0) {
                if (nb_octets == 0){
                    fprintf(stderr, ">>> Le serveur a fermé la connexion\n");
                }else{
                    perror("read");
                }

                // Fermeture et sortie
                close(descripteur_client);
                exit(EXIT_FAILURE);
            }

            // Affichage du message reçu
            write(STDOUT_FILENO, tampon, nb_octets);
        }

        // Données de l'utilisateur disponibles ?
        if (FD_ISSET(STDIN_FILENO, &ensemble_descripteurs)) {
            ssize_t nb_octets;
            unsigned char tampon[TAILLE_TAMPON];

            nb_octets = read(STDIN_FILENO, tampon, sizeof(tampon));
            if (nb_octets <= 0) {
                // L'utilisateur a fermé stdin (Ctrl-D)
                close(descripteur_client);
                exit(EXIT_SUCCESS);
            }

            // Envoi au serveur
            write(descripteur_client, tampon, nb_octets);
        }
    }

    return 0;
}
