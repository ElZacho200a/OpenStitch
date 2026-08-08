# SPDX-License-Identifier: Apache-2.0
import io
import json

from protocol import StdoutWriter, error_payload, read_requests


def test_stdout_writer_emits_one_json_line_per_call():
    buffer = io.StringIO()
    writer = StdoutWriter(buffer)
    writer.send({"type": "pong"})
    writer.send({"type": "ping"})
    lines = buffer.getvalue().splitlines()
    assert len(lines) == 2
    assert json.loads(lines[0]) == {"type": "pong"}
    assert json.loads(lines[1]) == {"type": "ping"}


def test_read_requests_skips_blank_lines_and_parses_json():
    stream = io.StringIO('{"type": "ping"}\n\n{"type": "pong"}\n')
    assert list(read_requests(stream)) == [{"type": "ping"}, {"type": "pong"}]


def test_read_requests_yields_parse_error_marker_for_malformed_json():
    stream = io.StringIO('{not json\n{"type": "ping"}\n')
    requests = list(read_requests(stream))
    assert "__parse_error__" in requests[0]
    assert requests[1] == {"type": "ping"}


def test_read_requests_rejects_non_object_json():
    stream = io.StringIO("[1, 2, 3]\n")
    requests = list(read_requests(stream))
    assert "__parse_error__" in requests[0]


def test_error_payload_shape():
    payload = error_payload("42", "CANCELLED", "Segmentation annulee.", "detail")
    assert payload == {
        "id": "42",
        "type": "error",
        "code": "CANCELLED",
        "message": "Segmentation annulee.",
        "details": "detail",
    }
