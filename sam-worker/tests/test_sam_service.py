# SPDX-License-Identifier: Apache-2.0
"""Ces tests supposent un environnement SANS sam2/torch installes (comme en
CI) : ils verifient donc explicitement le chemin d'erreur SAM2_NOT_INSTALLED,
pas le chargement reel d'un modele. Voir docs/source pour les tests manuels
avec un environnement complet."""
import numpy as np
import pytest

import model_registry
from sam_service import ModelError, SamService, load_image_array


def test_load_model_fails_with_checkpoint_not_found(tmp_path):
    service = SamService(tmp_path, device_preference="cpu")
    with pytest.raises(ModelError) as excinfo:
        service.load_model("tiny")
    assert excinfo.value.code == "CHECKPOINT_NOT_FOUND"


def test_load_model_rejects_unknown_model_id(tmp_path):
    service = SamService(tmp_path, device_preference="cpu")
    with pytest.raises(model_registry.UnknownModelError):
        service.load_model("huge")


def test_load_model_reports_sam2_not_installed_when_package_absent(tmp_path):
    (tmp_path / "sam2.1_hiera_tiny.pt").write_bytes(b"fake-checkpoint")
    service = SamService(tmp_path, device_preference="cpu")
    with pytest.raises(ModelError) as excinfo:
        service.load_model("tiny")
    assert excinfo.value.code == "SAM2_NOT_INSTALLED"


def test_generate_masks_without_loaded_model_raises_model_not_installed():
    service = SamService(".", device_preference="cpu")
    with pytest.raises(ModelError) as excinfo:
        service.generate_masks(np.zeros((5, 5, 3), dtype=np.uint8))
    assert excinfo.value.code == "MODEL_NOT_INSTALLED"


def test_unload_model_is_a_no_op_when_nothing_is_loaded():
    service = SamService(".", device_preference="cpu")
    service.unload_model()  # ne doit pas lever
    assert service.loaded_model_id is None


def test_load_image_array_is_writable_and_contiguous(tmp_path):
    from PIL import Image

    Image.fromarray(np.zeros((5, 5, 3), dtype=np.uint8) + 10, mode="RGB").save(tmp_path / "img.png")
    array = load_image_array(tmp_path / "img.png")
    assert array.flags["WRITEABLE"]
    assert array.flags["C_CONTIGUOUS"]
    assert array.shape == (5, 5, 3)


def test_load_image_array_downscales_to_max_resolution(tmp_path):
    from PIL import Image

    Image.fromarray(np.zeros((100, 50, 3), dtype=np.uint8), mode="RGB").save(tmp_path / "img.png")
    array = load_image_array(tmp_path / "img.png", max_resolution=20)
    assert max(array.shape[0], array.shape[1]) <= 20


def test_load_image_array_missing_file_raises_model_error(tmp_path):
    with pytest.raises(ModelError) as excinfo:
        load_image_array(tmp_path / "nope.png")
    assert excinfo.value.code == "IMAGE_LOAD_FAILED"
