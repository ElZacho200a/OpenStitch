# SPDX-License-Identifier: Apache-2.0
"""Catalogue central des tailles de modele SAM 2.1.

Doit rester en accord avec libs/ai_segmentation/src/model_catalog.cpp cote
C++ (memes noms de fichiers exacts). Le worker ne recoit jamais qu'un
identifiant de taille ("tiny" / "small" / "base_plus" / "large"), jamais un
couple (config, checkpoint) choisi independamment : c'est ce qui rend un
mauvais appariement config/checkpoint structurellement impossible.
"""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class ModelDescriptor:
    worker_id: str
    display_name: str
    config_name: str
    checkpoint_file: str


# config_name est le chemin EXACT attendu par `sam2.build_sam.build_sam2()` :
# relatif a la racine du paquet `sam2` installe, prefixe "configs/" inclus
# (verifie empiriquement -- sam2/__init__.py appelle
# `initialize_config_module("sam2")`, dont la racine de recherche Hydra est
# la racine du paquet, pas son sous-dossier configs/).
CATALOG: dict[str, ModelDescriptor] = {
    "tiny": ModelDescriptor("tiny", "Tiny", "configs/sam2.1/sam2.1_hiera_t.yaml",
                           "sam2.1_hiera_tiny.pt"),
    "small": ModelDescriptor("small", "Small", "configs/sam2.1/sam2.1_hiera_s.yaml",
                            "sam2.1_hiera_small.pt"),
    "base_plus": ModelDescriptor(
        "base_plus", "Base+", "configs/sam2.1/sam2.1_hiera_b+.yaml", "sam2.1_hiera_base_plus.pt"
    ),
    "large": ModelDescriptor(
        "large", "Large", "configs/sam2.1/sam2.1_hiera_l.yaml", "sam2.1_hiera_large.pt"
    ),
}


class UnknownModelError(ValueError):
    """`worker_id` ne correspond a aucune entree du catalogue."""


def resolve(worker_id: str) -> ModelDescriptor:
    try:
        return CATALOG[worker_id]
    except KeyError as exc:
        raise UnknownModelError(worker_id) from exc


def checkpoint_path(models_dir: Path, worker_id: str) -> Path:
    return Path(models_dir) / resolve(worker_id).checkpoint_file


def is_installed(models_dir: Path, worker_id: str) -> bool:
    return checkpoint_path(models_dir, worker_id).is_file()
