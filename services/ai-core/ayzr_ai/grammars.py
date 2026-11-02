"""GBNF grammars for the local runtime.

This is the single biggest quality win available to us, and it is only possible
because AYZR owns its model runtime. A grammar constrains the decoder itself, so
the model is physically unable to emit a token outside the allowed set.

Measured effect: with no grammar, Qwen2.5-1.5B produced
    "البنك المركزي يsignal خفض الفائدة"   (Arabic with an English word injected)
With the Arabic-only grammar, the same prompt produced pure Arabic.

An external OpenAI-compatible provider cannot do this, which is exactly why the
production path is our own runtime.
"""

from __future__ import annotations

# Arabic-only analysis object: summary_ar, direction, confidence_reason_ar,
# facts, inferences. String values admit Arabic letters, spaces, digits and
# punctuation, and nothing else.
ARABIC_ANALYSIS_GRAMMAR = r"""
root ::= "{" sp "\"summary_ar\"" sp ":" sp arstr sp "," sp "\"direction\"" sp ":" sp dir sp "," sp "\"confidence_reason_ar\"" sp ":" sp arstr sp "," sp "\"facts\"" sp ":" sp arstr sp "," sp "\"inferences\"" sp ":" sp arstr sp "}"
arstr ::= "\"" artext "\""
artext ::= (arw | sp | digit | punct)+
arw ::= [\u0600-\u06FF]
dir ::= "\"bullish\"" | "\"bearish\"" | "\"sideways\"" | "\"no_trade\""
sp ::= [ \t\n]*
digit ::= [0-9]
punct ::= [.،؛:!؟\-٪%()]
"""

# Arabic-only free prose: a single string. Used when we want an explanation with
# no structure at all.
ARABIC_PROSE_GRAMMAR = r"""
root ::= artext
artext ::= (arw | sp | digit | punct)+
arw ::= [\u0600-\u06FF]
sp ::= [ \t\n]*
digit ::= [0-9]
punct ::= [.،؛:!؟\-٪%()]
"""


def grammar_for(agent_name: str, json_output: bool) -> str:
    """Pick the grammar for an agent, or none if structure is not required."""
    if json_output:
        return ARABIC_ANALYSIS_GRAMMAR
    return ARABIC_PROSE_GRAMMAR
