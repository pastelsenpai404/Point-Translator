"""Loopback-only Laya decision service for Point Translator."""

import json
from http.server import BaseHTTPRequestHandler, HTTPServer

from laya import Router


QUESTIONS = {
    "intent": {
        "type": "choice",
        "instructions": "What is the main intent of this message?",
        "criteria": {
            "question": "asks for an answer, explanation, or information",
            "request": "asks someone to do something or provide help",
            "complaint": "reports dissatisfaction, a problem, or an error",
            "statement": "shares information, a greeting, or casual conversation",
        },
    },
    "urgency": {
        "type": "choice",
        "instructions": "How soon does this message need a response?",
        "criteria": {
            "normal": "no deadline or immediate consequence",
            "soon": "asks for a prompt response or mentions a near deadline",
            "urgent": "immediate deadline, blocked work, or an emergency",
        },
    },
}

router = Router()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass  # Never log the text being translated.

    def respond(self, status, payload):
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/health":
            self.respond(200, {"service": "point-translator-laya"})
        else:
            self.respond(404, {"error": "not found"})

    def do_POST(self):
        if self.path != "/analyze":
            self.respond(404, {"error": "not found"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= 65536:
                self.respond(413, {"error": "invalid request size"})
                return
            request = json.loads(self.rfile.read(length))
            state = request.get("text")
            lang = request.get("lang")
            if not isinstance(state, str) or not 0 < len(state) <= 4000:
                self.respond(400, {"error": "text must contain 1 to 4000 characters"})
                return
            if lang not in ("zh", "en", "th"):
                self.respond(400, {"error": "unsupported language"})
                return
            answer = router.predict(state, QUESTIONS, lang=lang)["answers"]
            self.respond(200, {
                "intent": answer["intent"]["choice"],
                "urgency": answer["urgency"]["choice"],
            })
        except (KeyError, TypeError, ValueError):
            self.respond(400, {"error": "invalid request or model result"})
        except Exception as exc:
            self.respond(503, {"error": f"{type(exc).__name__}: {exc}"})


if __name__ == "__main__":
    HTTPServer(("127.0.0.1", 18766), Handler).serve_forever()
