# Lancement d'un serveur et de 6 clients

## Clients
1. ed
2. bob
3. clem
4. ana
5. zoe
6. test

---

## Scénario : découverte du menu principal (avec Zoe)

Menu principal — options disponibles :

1. Voir son profil
2. Lister les utilisateurs connectés
3. Défier un utilisateur (ex. : on choisit Ana) — *opération asynchrone*
4. Consulter le profil d'un utilisateur (ex. : Zoe)
5. Afficher les parties en cours (ici : aucune pour l'instant)
6. Gérer mes amis
    - Ajouter un ami (impossible de s'ajouter soi‑même ou d'ajouter un utilisateur qui n'existe pas)
    - Supprimer un ami
7. Définir / modifier sa bio
8. Regarder une partie en cours (si aucune partie, l'action échoue)
9. Envoyer un message au lobby
    - Ex. : tout le monde reçoit le message de Zoe
10. Modifier les droits de visionnage (autoriser tout le monde ou restreindre aux amis)
11. Quitter

---

## Déroulement typique

- On consulte le profil d'Ana pour vérifier ses informations.
- Si Ana a une demande de défi en attente, on peut l'accepter ; l'acceptation lance la partie.
- Pendant la partie :
    - Les deux joueuses peuvent s'envoyer des messages privés (non visibles dans le lobby).
    - Les spectateurs autorisés peuvent demander à regarder la partie.
- Exemple : Test demande à regarder la partie entre Ana et Zoe → il voit le plateau et les messages privés ; à chaque coup joué par Ana ou Zoe, le plateau se met à jour pour le spectateur.
- Une autre partie démarre entre ed et bob ; ils échangent aussi des messages privés.
- Quand clem regarde les 3 parties en cours il voit les scores mis à jour pour chaque partie.
- Le serveur gère plusieurs parties en parallèle sans problème.

---

## Implémentations supplémentaires / règles côté client

- Un joueur ne peut pas challenger plusieurs adversaires en même temps (contrainte appliquée côté client).
- Un joueur ne peut pas challenger un adversaire déjà en jeu (le serveur peut refuser la demande).
- Si un joueur A a envoyé un défi et en reçoit un autre pendant qu'il attend une réponse, accepter le nouveau défi annule automatiquement l'ancien (le serveur notifie l'ancien défié si nécessaire).
- Un joueur ne peut pas se défier lui‑même ni demander son propre profil.
- Le score des parties en cours est mis à jour à chaque requête de la liste des parties en cours.

---
