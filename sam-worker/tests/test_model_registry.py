# SPDX-License-Identifier: Apache-2.0
import pytest

import model_registry


def test_catalog_has_exact_filenames_for_every_size():
    assert model_registry.resolve("tiny").config_name == "configs/sam2.1/sam2.1_hiera_t.yaml"
    assert model_registry.resolve("tiny").checkpoint_file == "sam2.1_hiera_tiny.pt"
    assert model_registry.resolve("small").config_name == "configs/sam2.1/sam2.1_hiera_s.yaml"
    assert model_registry.resolve("small").checkpoint_file == "sam2.1_hiera_small.pt"
    assert model_registry.resolve("base_plus").config_name == "configs/sam2.1/sam2.1_hiera_b+.yaml"
    assert model_registry.resolve("base_plus").checkpoint_file == "sam2.1_hiera_base_plus.pt"
    assert model_registry.resolve("large").config_name == "configs/sam2.1/sam2.1_hiera_l.yaml"
    assert model_registry.resolve("large").checkpoint_file == "sam2.1_hiera_large.pt"


def test_resolve_rejects_unknown_model_id():
    with pytest.raises(model_registry.UnknownModelError):
        model_registry.resolve("huge")


def test_is_installed_reflects_checkpoint_presence(tmp_path):
    assert model_registry.is_installed(tmp_path, "tiny") is False
    (tmp_path / "sam2.1_hiera_tiny.pt").write_bytes(b"fake-checkpoint")
    assert model_registry.is_installed(tmp_path, "tiny") is True
