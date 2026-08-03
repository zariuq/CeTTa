from types import SimpleNamespace


class _Responses:
    def create(self, **arguments):
        prompt = str(arguments.get("input", "")).lower()
        if "stockholm" in prompt:
            output = "(in stockholm sweden)"
        elif "vienna" in prompt:
            output = "(in vienna austria)"
        else:
            raise ValueError("fixture only recognizes stockholm and vienna")
        return SimpleNamespace(output_text=output)


class OpenAI:
    def __init__(self, *arguments, **keywords):
        del arguments, keywords
        self.responses = _Responses()
