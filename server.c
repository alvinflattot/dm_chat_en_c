// server.c
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
#define MAX_NOM_SALON 32

// Rôles possibles pour les utilisateurs
#define ROLE_UTILISATEUR 0
#define ROLE_MODERATEUR 1
#define ROLE_ADMIN 2

// Structure représentant un client connecté
typedef struct salon salon_t;
typedef struct client {
    int descripteur;
    char pseudo[MAX_PSEUDO];
    salon_t *salon_courant;
} client_t;

// Association client + rôle dans un salon
typedef struct client_role {
    client_t *client;
    int role;
} client_role_t;

// Structure représentant un salon de discussion
struct salon {
    char nom_salon[MAX_NOM_SALON];
    client_role_t clients_dans_salon[MAX_CLIENTS];
    int nb_clients;
};

// Listes globales de clients et salons
static client_t *liste_clients[MAX_CLIENTS];
static salon_t *liste_salons[MAX_SALONS];
static int nb_clients_total = 0;
static int nb_salons_total = 0;
static pthread_mutex_t mutex_global = PTHREAD_MUTEX_INITIALIZER;
static salon_t *salon_par_defaut = NULL;

/**
 * Renvoie un préfixe selon le rôle (pour distinguer admin/modérateur)
 */
const char *obtenir_prefixe_selon_role(int role) {
    switch (role) {
        case ROLE_ADMIN: return "@";
        case ROLE_MODERATEUR: return "#";
        default: return "";
    }
}

/**
 * Recherche un salon par son nom
 */
salon_t *trouver_salon(const char *nom) {
    for (int i = 0; i < nb_salons_total; i++) {
        if (strcmp(liste_salons[i]->nom_salon, nom) == 0) {
            return liste_salons[i];
        }
    }
    return NULL;
}

/**
 * Recherche le rôle d'un client dans un salon donné
 */
int obtenir_role_dans_salon(salon_t *salon, client_t *client) {
    for (int i = 0; i < salon->nb_clients; i++) {
        if (salon->clients_dans_salon[i].client == client) {
            return salon->clients_dans_salon[i].role;
        }
    }
    return ROLE_UTILISATEUR;
}

/**
 * Crée un nouveau salon ou retourne un salon existant
 */
salon_t *obtenir_ou_creer_salon(const char *nom) {
    salon_t *s = trouver_salon(nom);
    if (s) return s;
    if (nb_salons_total >= MAX_SALONS) return NULL;
    s = malloc(sizeof(salon_t));
    if (!s) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    strncpy(s->nom_salon, nom, MAX_NOM_SALON);
    s->nb_clients = 0;
    liste_salons[nb_salons_total++] = s;
    return s;
}

/**
 * Diffuse un message à tous les membres d'un salon
 * @param salon      Le salon cible
 * @param message    Le message à envoyer
 * @param desource_exclure  Descripteur à exclure (-1 pour tous)
 */
void diffuser_message_dans_salon(salon_t *salon, const char *message, int descripteur_exclure) {
    for (int i = 0; i < salon->nb_clients; i++) {
        int dsock = salon->clients_dans_salon[i].client->descripteur;
        if (descripteur_exclure < 0 || dsock != descripteur_exclure) {
            write(dsock, message, strlen(message));
        }
    }
}

/**
 * Ajoute un client à un salon et notifie les autres
 */
void ajouter_client_au_salon(salon_t *salon, client_t *client) {
    int role_initial = (salon != salon_par_defaut && salon->nb_clients == 0) ? ROLE_ADMIN : ROLE_UTILISATEUR;
    salon->clients_dans_salon[salon->nb_clients].client = client;
    salon->clients_dans_salon[salon->nb_clients].role = role_initial;
    salon->nb_clients++;
    client->salon_courant = salon;

    char message[MAX_MESSAGE];
    snprintf(message, sizeof(message), "%s%s s'est connecté(e) dans %s.\n", obtenir_prefixe_selon_role(role_initial), client->pseudo, salon->nom_salon);
    diffuser_message_dans_salon(salon, message, client->descripteur);
}

/**
 * Retire un client d'un salon et notifie les autres
 */
void retirer_client_du_salon(client_t *client) {
    salon_t *salon = client->salon_courant;
    if (!salon) return;

    char message[MAX_MESSAGE];
    snprintf(message, sizeof(message), "%s s'est déconnecté(e) de %s.\n", client->pseudo, salon->nom_salon);
    diffuser_message_dans_salon(salon, message, client->descripteur);

    // Supprime le client de la liste du salon
    for (int i = 0; i < salon->nb_clients; i++) {
        if (salon->clients_dans_salon[i].client == client) {
            salon->clients_dans_salon[i] = salon->clients_dans_salon[--salon->nb_clients];
            break;
        }
    }

    // Si le salon (hors salon par défaut) est vide, le libérer
    if (salon != salon_par_defaut && salon->nb_clients == 0) {
        for (int i = 0; i < nb_salons_total; i++) {
            if (liste_salons[i] == salon) {
                free(salon);
                liste_salons[i] = liste_salons[--nb_salons_total];
                break;
            }
        }
    }
    client->salon_courant = NULL;
}

/**
 * Vérifie si un pseudo est déjà utilisé
 */
int existe_deja_le_pseudo(const char *pseudo) {
    for (int i = 0; i < nb_clients_total; i++) {
        if (strcmp(liste_clients[i]->pseudo, pseudo) == 0) {
            return 1;
        }
    }
    return 0;
}

/**
 * Envoie la liste des salons disponibles au client
 */
void envoyer_liste_des_salons(int descripteur_client) {
    char tampon[MAX_MESSAGE] = "Salons disponibles:\n";
    for (int i = 0; i < nb_salons_total; i++) {
        strcat(tampon, "- ");
        strcat(tampon, liste_salons[i]->nom_salon);
        strcat(tampon, "\n");
    }
    write(descripteur_client, tampon, strlen(tampon));
}

/**
 * Fonction exécutée pour chaque client dans un thread séparé
 */
void *gerer_un_client(void *arg) {
    int descripteur = *(int *)arg;
    free(arg);  // libération de la mémoire allouée pour le descripteur
    char tampon[MAX_MESSAGE];
    ssize_t nb_octets;
    char pseudo[MAX_PSEUDO];

    // Demander un pseudo unique
    do {
        write(descripteur, "Entrez votre pseudo : ", 21);
        nb_octets = read(descripteur, pseudo, sizeof(pseudo));
        if (nb_octets <= 0) {
            close(descripteur);
            return NULL;
        }
        pseudo[strcspn(pseudo, "\r\n")] = '\0';

        pthread_mutex_lock(&mutex_global);
        if (existe_deja_le_pseudo(pseudo)) {
            pthread_mutex_unlock(&mutex_global);
            write(descripteur, "Pseudo déjà pris.\n", 18);
        } else {
            pthread_mutex_unlock(&mutex_global);
            break;
        }
    } while (1);

    // Création et ajout du client à la liste globale
    client_t *client = malloc(sizeof(client_t));
    if (!client) {
		perror("malloc");
		close(descripteur); return NULL;
	}
    client->descripteur = descripteur;
    strncpy(client->pseudo, pseudo, MAX_PSEUDO);

    pthread_mutex_lock(&mutex_global);
    liste_clients[nb_clients_total++] = client;
    ajouter_client_au_salon(salon_par_defaut, client);
    pthread_mutex_unlock(&mutex_global);

    // Envoi du message de bienvenue
    snprintf(tampon, sizeof(tampon), "Bienvenue %s dans %s !\n", client->pseudo, salon_par_defaut->nom_salon);
    write(descripteur, tampon, strlen(tampon));

    // Boucle de réception des commandes/messages du client
    while ((nb_octets = read(descripteur, tampon, sizeof(tampon)-1)) > 0) {
        tampon[nb_octets] = '\0';
        tampon[strcspn(tampon, "\r\n")] = '\0';

        pthread_mutex_lock(&mutex_global);

        // Gestion des différentes commandes
        if (strcmp(tampon, "/exit") == 0) {
            // /exit : déconnexion propre
            pthread_mutex_unlock(&mutex_global);
            break;

        } else if (strcmp(tampon, "/channels") == 0) {
            // /channels : lister tous les salons
            envoyer_liste_des_salons(descripteur);

        } else if (strncmp(tampon, "/join channel", 6) == 0) {
            // /join <salon> : quitter l'ancien salon et rejoindre (ou créer) le nouveau
            char *nom_salon = tampon + 6;
            retirer_client_du_salon(client);
            salon_t *salon_cible = obtenir_ou_creer_salon(nom_salon);
            if (salon_cible == NULL) {
                write(descripteur, "Impossible de créer ou rejoindre le salon.\n", strlen("Impossible de créer ou rejoindre le salon.\n"));
            } else {
                ajouter_client_au_salon(salon_cible, client);
                char msg_confirm[MAX_MESSAGE];
                snprintf(msg_confirm, sizeof(msg_confirm), "Vous avez rejoint %s.\n", salon_cible->nom_salon);
                write(descripteur, msg_confirm, strlen(msg_confirm));
            }

        }  else if (strcmp(tampon, "/date") == 0) {
            // /date : envoyer la date et l'heure du serveur
            time_t maintenant = time(NULL);
            char msg_date[MAX_MESSAGE];
            strftime(msg_date, sizeof(msg_date), "Date serveur : %d/%m/%Y %H:%M:%S\n", localtime(&maintenant));
            write(descripteur, msg_date, strlen(msg_date));

        } else if (strncmp(tampon, "/kick ", 6) == 0) {
            // /kick <pseudo> : expulser un utilisateur du salon courant
            if (client->salon_courant == salon_par_defaut) {
                write(descripteur, "Commande indisponible dans le salon par défaut.\n", strlen("Commande indisponible dans le salon par défaut.\n"));
            } else {
                int mon_role = obtenir_role_dans_salon(client->salon_courant, client);
                if (mon_role < ROLE_MODERATEUR) {
                    write(descripteur, "Permission refusée.\n", strlen("Permission refusée.\n"));
                } else {
                    char *pseudo_cible = tampon + 6;
                    salon_t *salon_actuel = client->salon_courant;
                    for (int i = 0; i < salon_actuel->nb_clients; i++) {
                        if (strcmp(salon_actuel->clients_dans_salon[i].client->pseudo, pseudo_cible) == 0) {
                            client_t *cible = salon_actuel->clients_dans_salon[i].client;
                            retirer_client_du_salon(cible);
                            ajouter_client_au_salon(salon_par_defaut, cible);
                            write(cible->descripteur, "Vous avez été expulsé du salon.\n", strlen("Vous avez été expulsé du salon.\n"));

                            char annonce[MAX_MESSAGE];
                            snprintf(annonce, sizeof(annonce), "%s a été expulsé du salon.\n", pseudo_cible);
                            diffuser_message_dans_salon(salon_actuel, annonce, -1);
                            break;
                        }
                    }
                }
            }

        } else if (strncmp(tampon, "/ban ", 5) == 0) {
            // /ban <pseudo> : placeholder (non implémenté)
            write(descripteur, "Fonction bannir non implémentée.\n", strlen("Fonction bannir non implémentée.\n"));

        } else if (strncmp(tampon, "/promote ", 9) == 0) {
            // /promote <pseudo> : élever au rôle de modérateur ou admin
            if (client->salon_courant == salon_par_defaut) {
                write(descripteur, "Commande indisponible dans le salon par défaut.\n", strlen("Commande indisponible dans le salon par défaut.\n"));
            } else {
                int mon_role = obtenir_role_dans_salon(client->salon_courant, client);
                if (mon_role < ROLE_MODERATEUR) {
                    write(descripteur, "Permission refusée.\n", strlen("Permission refusée.\n"));
                } else {
                    char *pseudo_cible = tampon + 9;
                    salon_t *salon_actuel = client->salon_courant;
                    for (int i = 0; i < salon_actuel->nb_clients; i++) {
                        if (strcmp(salon_actuel->clients_dans_salon[i].client->pseudo, pseudo_cible) == 0) {
                            if (mon_role == ROLE_MODERATEUR && salon_actuel->clients_dans_salon[i].role < ROLE_MODERATEUR) {
                                salon_actuel->clients_dans_salon[i].role = ROLE_MODERATEUR;
                                char annonce[MAX_MESSAGE];
                                snprintf(annonce, sizeof(annonce), "%s est maintenant modérateur.\n", pseudo_cible);
                                diffuser_message_dans_salon(salon_actuel, annonce, -1);
                            } else if (mon_role == ROLE_ADMIN) {
                                salon_actuel->clients_dans_salon[i].role = ROLE_ADMIN;
                                char annonce[MAX_MESSAGE];
                                snprintf(annonce, sizeof(annonce), "%s est maintenant administrateur.\n", pseudo_cible);
                                diffuser_message_dans_salon(salon_actuel, annonce, -1);
                            }
                            break;
                        }
                    }
                }
            }

        } else if (strcmp(tampon, "/destroy") == 0) {
            // /destroy : détruire le salon entier (admin uniquement)
            if (client->salon_courant == salon_par_defaut) {
                write(descripteur, "Impossible de détruire le salon par défaut.\n", strlen("Impossible de détruire le salon par défaut.\n"));
            } else if (obtenir_role_dans_salon(client->salon_courant, client) != ROLE_ADMIN) {
                write(descripteur, "Seul l'administrateur peut détruire ce salon.\n", strlen("Seul l'administrateur peut détruire ce salon.\n"));
            } else {
                salon_t *a_detruire = client->salon_courant;
                // Déplacer tous les membres dans le salon par défaut
                for (int i = a_detruire->nb_clients - 1; i >= 0; i--) {
                    client_t *cible = a_detruire->clients_dans_salon[i].client;
                    retirer_client_du_salon(cible);
                    ajouter_client_au_salon(salon_par_defaut, cible);
                    write(cible->descripteur, "Salon supprimé par l'administrateur. Vous êtes déplacé dans le salon par défaut.\n", strlen("Salon supprimé par l'administrateur. Vous êtes déplacé dans le salon par défaut.\n"));
                }
                printf(">>> Salon %s détruit par %s\n",
                       a_detruire->nom_salon, client->pseudo);
            }

        } else {
            // Diffusion d'un message normal à tout le salon
            int role = obtenir_role_dans_salon(client->salon_courant, client);
            char message[MAX_MESSAGE + MAX_PSEUDO + 4];
            snprintf(message, sizeof(message), "%s%s: %s\n", obtenir_prefixe_selon_role(role), client->pseudo, tampon);
            diffuser_message_dans_salon(client->salon_courant, message, descripteur);
        }

        pthread_mutex_unlock(&mutex_global);

    }

    // Nettoyage à la déconnexion du client
    pthread_mutex_lock(&mutex_global);
    retirer_client_du_salon(client);
    for (int i = 0; i < nb_clients_total; i++) {
        if (liste_clients[i] == client) {
            liste_clients[i] = liste_clients[--nb_clients_total];
            break;
        }
    }
    pthread_mutex_unlock(&mutex_global);

    close(descripteur);
    free(client);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage : %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    srand((unsigned)time(NULL));
    unsigned short port_serveur = (unsigned short)atoi(argv[1]);

    // Création de la socket d'écoute
    int descripteur_serveur = socket(AF_INET, SOCK_STREAM, 0);
    if (descripteur_serveur == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Autoriser la réutilisation rapide de l'adresse
    int opt = 1;
    setsockopt(descripteur_serveur, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in adresse;
    memset(&adresse, 0, sizeof(adresse));
    adresse.sin_family = AF_INET;
    adresse.sin_port = htons(port_serveur);
    adresse.sin_addr.s_addr = INADDR_ANY;

    if (bind(descripteur_serveur, (struct sockaddr *)&adresse, sizeof(adresse)) == -1) {
        perror("bind"); close(descripteur_serveur); exit(EXIT_FAILURE);
    }
    if (listen(descripteur_serveur, 10) == -1) {
        perror("listen"); close(descripteur_serveur); exit(EXIT_FAILURE);
    }

    printf(">>> Serveur en écoute sur le port %d...\n", port_serveur);

    // Création du salon par défaut (lobby)
    salon_par_defaut = obtenir_ou_creer_salon("lobby");

    // Boucle d'acceptation des connexions entrantes
    while (1) {
        struct sockaddr_in addr_client;
        socklen_t taille = sizeof(addr_client);
        int *pointeur_desc = malloc(sizeof(int));
        if (!pointeur_desc) { perror("malloc"); continue; }
        *pointeur_desc = accept(descripteur_serveur, (struct sockaddr *)&addr_client, &taille);
        if (*pointeur_desc == -1) {
            perror("accept"); free(pointeur_desc); continue;
        }
        pthread_t id_thread;
        pthread_create(&id_thread, NULL, gerer_un_client, pointeur_desc);
        pthread_detach(id_thread);
    }

    close(descripteur_serveur);
    return 0;
}