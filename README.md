Le module app_options s'installe de la manière suivante:
	déplacer le fichier app_options.c -> /usr/src/asterisk-${version}/apps/
	déplacer le fichier app_options.h -> /usr/src/asterisk-${version}/include/asterisk/
Commencer l'installation d'asterisk:
	./configure
	make menuselect -> applications -> cocher app_options
	make && make install
Ouvrir asterisk et verifier que l'application Options a bien été chargé en fesant :
    core show application Options
	
En case de problème:
    module load app_options.so (Voir erreur dans la console asterisk)
	Vérifier que le fichier options.conf est bien enregistré dans le chemin /etc/asterisk/

Bonne chance[Jazzar Wessim]
