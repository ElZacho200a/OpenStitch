# Copie incrémentale de la bibliothèque standard Python (SCRIPT-01), appelée par
# openstitch_deploy_python : cmake -DSOURCE=<Lib> -DDESTINATION=<exe>/python/Lib -P <ce fichier>.
# file(COPY) ne recopie pas un fichier inchangé ; les suites de tests, l'IDE, Tk et les
# caches de bytecode sont exclus (inutiles à un script de sélection, plusieurs dizaines de Mo).
if(NOT SOURCE OR NOT DESTINATION)
    message(FATAL_ERROR "deploy_python_stdlib : SOURCE et DESTINATION requis")
endif()
file(MAKE_DIRECTORY "${DESTINATION}")
file(COPY "${SOURCE}/" DESTINATION "${DESTINATION}"
    PATTERN "__pycache__" EXCLUDE
    PATTERN "test" EXCLUDE
    PATTERN "tests" EXCLUDE
    PATTERN "idlelib" EXCLUDE
    PATTERN "tkinter" EXCLUDE
    PATTERN "turtledemo" EXCLUDE
    PATTERN "ensurepip" EXCLUDE
    PATTERN "site-packages" EXCLUDE
    PATTERN "*.pyc" EXCLUDE)
