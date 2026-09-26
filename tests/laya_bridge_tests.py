"""Check the local HTTP contract without loading or downloading a model."""

import importlib.util
import json
import pathlib
import sys
import threading
import types
import unittest
from http.server import HTTPServer
from urllib.error import HTTPError
from urllib.request import Request, urlopen


class FakeRouter:
    def predict(self, state, questions, lang=None, model=None):
        if isinstance(state, dict):
            assert state == {"source": "你好", "source_lang": "zh", "target_lang": "en"}
            assert questions["best"]["criteria"] == {"argos": "Hello", "ai": "Hi"}
            assert model == "multilingual"
            return {"answers": {"best": {"choice": "ai"}}}
        assert state == "你好，今天有空吗？"
        assert lang == "zh"
        assert set(questions) == {"intent", "urgency"}
        return {"answers": {
            "intent": {"choice": "question"},
            "urgency": {"choice": "normal"},
        }}


sys.modules["laya"] = types.SimpleNamespace(Router=FakeRouter)
bridge_path = pathlib.Path(__file__).resolve().parents[1] / "laya" / "bridge.py"
spec = importlib.util.spec_from_file_location("point_laya_bridge", bridge_path)
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)


class BridgeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = HTTPServer(("127.0.0.1", 0), bridge.Handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.url = f"http://127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def test_health_and_multilingual_analysis(self):
        with urlopen(self.url + "/health") as response:
            self.assertEqual(json.load(response)["service"], "point-translator-laya")
        body = json.dumps({"text": "你好，今天有空吗？", "lang": "zh"}).encode()
        with urlopen(Request(self.url + "/analyze", body, {"Content-Type": "application/json"})) as response:
            self.assertEqual(json.load(response), {"intent": "question", "urgency": "normal"})

    def test_invalid_input_is_rejected(self):
        body = json.dumps({"text": "", "lang": "en"}).encode()
        with self.assertRaises(HTTPError) as error:
            urlopen(Request(self.url + "/analyze", body))
        self.assertEqual(error.exception.code, 400)

    def test_selects_one_supplied_translation(self):
        body = json.dumps({"source": "你好", "lang": "zh", "target": "en",
                           "argos": "Hello", "ai": "Hi"}).encode()
        with urlopen(Request(self.url + "/select-translation", body)) as response:
            self.assertEqual(json.load(response), {"choice": "ai"})

    def test_rejects_missing_translation_candidate(self):
        body = json.dumps({"source": "你好", "lang": "zh", "target": "en",
                           "argos": "Hello"}).encode()
        with self.assertRaises(HTTPError) as error:
            urlopen(Request(self.url + "/select-translation", body))
        self.assertEqual(error.exception.code, 400)


if __name__ == "__main__":
    unittest.main()
