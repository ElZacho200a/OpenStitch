## Contexte et entrée de roadmap

- Problème résolu :
- Entrée `HP-XXX-NNN` concernée (ou « roadmap : aucune entrée concernée », justifier) :

## Modifications livrées

- Code et câblage bureau/CLI :
- Format `.osp`, compatibilité et déterminisme (si touchés) :

## Vérifications effectuées

- [ ] Tests ciblés lancés (commande + résultat, ou expliquer pourquoi impossible)
- [ ] Tests de non-régression et CI vérifiés
- [ ] Test Qt (QTest) headless si l'interface change
- [ ] `clang-format` 18 vérifié sur les fichiers C++ modifiés
- [ ] Roadmap et documentation actualisées
- [ ] Pas de dépendance GPL/AGPL et aucune mutation de projet hors `ICommand`

## Limites et validation humaine

- Essai machine réel effectué ? Si non, indiquer ce qui reste à vérifier :
- Captures/SVG et points de vigilance pour la revue :

## UX Gate — obligatoire si une interface Studio ou son workflow change

> Gate automatique : voir [docs/agents/ux-gate.md](../docs/agents/ux-gate.md).
> Sur une PR non UI, laisser cette section non renseignée ; elle est ignorée par la CI.

- Parcours novice : TODO — décrire un scénario exécuté depuis une image, avec résultat.
- Clics avant/après : TODO — mesurer les gestes du même scénario (exemple 8 -> 4).
- Captures inspectées : TODO — lien d'artefact openstitch-studio-screenshots-windows et/ou captures locales réelles inspectées.
- Revue indépendante : TODO — nom de l'agent distinct, verdict PASS/FRICTION/BLOQUANT et preuve.
- Tests UX : TODO — suites QTest, scénario natif, résultat réel et limites.
