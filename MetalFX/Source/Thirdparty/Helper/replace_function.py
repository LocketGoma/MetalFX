#!/usr/bin/env python3

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


#If You Need Debug Settings Edit to "True"
DEBUG = False


@dataclass
class FunctionSignature:
    return_type: str
    function_name: str
    parameter_types: list[str]


@dataclass
class FunctionCandidate:
    start_index: int
    header_end_index: int
    body_end_index: int
    raw_header: str
    return_type: str
    function_name: str
    parameter_types: list[str]


def debug(message: str) -> None:
    if DEBUG:
        print(f"[DEBUG] {message}", flush=True)


def info(message: str) -> None:
    print(f"[INFO] {message}", flush=True)


def error(message: str) -> None:
    print(f"[ERROR] {message}", file=sys.stderr, flush=True)


def strip_comments(text: str) -> str:
    text = re.sub(r"//.*", "", text)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return text


def normalize_whitespace(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def normalize_type(text: str) -> str:
    text = strip_comments(text)
    text = normalize_whitespace(text)

    removable_keywords = {
        "inline",
        "static",
        "extern",
        "virtual",
        "explicit",
        "friend",
        "register",
        "mutable",
        "constexpr",
        "consteval",
        "constinit",
        "FORCEINLINE",
        "FORCEINLINE_DEBUGGABLE",
        "UE_DEPRECATED",
    }

    tokens = text.split()
    tokens = [token for token in tokens if token not in removable_keywords]
    text = " ".join(tokens)

    text = re.sub(r"\s*::\s*", "::", text)
    text = re.sub(r"\s*<\s*", "<", text)
    text = re.sub(r"\s*>\s*", ">", text)
    text = re.sub(r"\s*,\s*", ",", text)
    text = re.sub(r"\s*\*\s*", "*", text)
    text = re.sub(r"\s*&\s*", "&", text)
    text = re.sub(r"\s*\[\s*", "[", text)
    text = re.sub(r"\s*\]\s*", "]", text)
    text = normalize_whitespace(text)

    return text


def remove_default_value(param: str) -> str:
    depth_angle = 0
    depth_paren = 0
    depth_bracket = 0
    depth_brace = 0
    in_str: Optional[str] = None
    escape = False

    for i, ch in enumerate(param):
        if in_str:
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == in_str:
                in_str = None
            continue

        if ch in ('"', "'"):
            in_str = ch
            continue

        if ch == "<":
            depth_angle += 1
        elif ch == ">":
            depth_angle = max(0, depth_angle - 1)
        elif ch == "(":
            depth_paren += 1
        elif ch == ")":
            depth_paren = max(0, depth_paren - 1)
        elif ch == "[":
            depth_bracket += 1
        elif ch == "]":
            depth_bracket = max(0, depth_bracket - 1)
        elif ch == "{":
            depth_brace += 1
        elif ch == "}":
            depth_brace = max(0, depth_brace - 1)
        elif (
            ch == "="
            and depth_angle == 0
            and depth_paren == 0
            and depth_bracket == 0
            and depth_brace == 0
        ):
            return param[:i].strip()

    return param.strip()


def split_top_level_commas(text: str) -> list[str]:
    parts: list[str] = []
    current: list[str] = []

    depth_angle = 0
    depth_paren = 0
    depth_bracket = 0
    depth_brace = 0
    in_str: Optional[str] = None
    escape = False

    i = 0
    while i < len(text):
        ch = text[i]

        if in_str:
            current.append(ch)
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == in_str:
                in_str = None
            i += 1
            continue

        if ch in ('"', "'"):
            in_str = ch
            current.append(ch)
            i += 1
            continue

        if ch == "<":
            depth_angle += 1
        elif ch == ">":
            depth_angle = max(0, depth_angle - 1)
        elif ch == "(":
            depth_paren += 1
        elif ch == ")":
            depth_paren = max(0, depth_paren - 1)
        elif ch == "[":
            depth_bracket += 1
        elif ch == "]":
            depth_bracket = max(0, depth_bracket - 1)
        elif ch == "{":
            depth_brace += 1
        elif ch == "}":
            depth_brace = max(0, depth_brace - 1)
        elif (
            ch == ","
            and depth_angle == 0
            and depth_paren == 0
            and depth_bracket == 0
            and depth_brace == 0
        ):
            part = "".join(current).strip()
            if part:
                parts.append(part)
            current = []
            i += 1
            continue

        current.append(ch)
        i += 1

    tail = "".join(current).strip()
    if tail:
        parts.append(tail)

    return parts


def find_matching_paren(text: str, open_index: int) -> Optional[int]:
    if open_index < 0 or open_index >= len(text) or text[open_index] != "(":
        return None

    depth = 0
    in_str: Optional[str] = None
    in_line_comment = False
    in_block_comment = False
    escape = False

    i = open_index
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""

        if in_line_comment:
            if ch == "\n":
                in_line_comment = False
            i += 1
            continue

        if in_block_comment:
            if ch == "*" and nxt == "/":
                in_block_comment = False
                i += 2
                continue
            i += 1
            continue

        if in_str:
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == in_str:
                in_str = None
            i += 1
            continue

        if ch == "/" and nxt == "/":
            in_line_comment = True
            i += 2
            continue

        if ch == "/" and nxt == "*":
            in_block_comment = True
            i += 2
            continue

        if ch in ('"', "'"):
            in_str = ch
            i += 1
            continue

        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                return i

        i += 1

    return None


def find_matching_brace(text: str, open_index: int) -> Optional[int]:
    if open_index < 0 or open_index >= len(text) or text[open_index] != "{":
        return None

    depth = 0
    in_str: Optional[str] = None
    in_line_comment = False
    in_block_comment = False
    escape = False

    i = open_index
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""

        if in_line_comment:
            if ch == "\n":
                in_line_comment = False
            i += 1
            continue

        if in_block_comment:
            if ch == "*" and nxt == "/":
                in_block_comment = False
                i += 2
                continue
            i += 1
            continue

        if in_str:
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == in_str:
                in_str = None
            i += 1
            continue

        if ch == "/" and nxt == "/":
            in_line_comment = True
            i += 2
            continue

        if ch == "/" and nxt == "*":
            in_block_comment = True
            i += 2
            continue

        if ch in ('"', "'"):
            in_str = ch
            i += 1
            continue

        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return i

        i += 1

    return None


def skip_function_suffix(text: str, index: int) -> int:
    i = index

    while i < len(text) and text[i].isspace():
        i += 1

    while True:
        before = i

        for keyword in ("const", "noexcept", "override", "final"):
            if re.match(rf"{keyword}\b", text[i:]):
                i += len(keyword)
                while i < len(text) and text[i].isspace():
                    i += 1

                if i < len(text) and text[i] == "(" and keyword == "noexcept":
                    close = find_matching_paren(text, i)
                    if close is None:
                        return i
                    i = close + 1
                    while i < len(text) and text[i].isspace():
                        i += 1
                break

        if text.startswith("requires", i):
            while i < len(text) and text[i] != "{":
                i += 1

        if i == before:
            break

    return i


def find_header_start(text: str, function_name_start: int) -> int:
    i = function_name_start - 1

    while i >= 0 and text[i].isspace():
        i -= 1

    while i >= 0:
        ch = text[i]

        if ch in ";{}":
            return skip_leading_whitespace(text, i + 1)

        if ch == "\n":
            line_start = text.rfind("\n", 0, i)
            line_start = 0 if line_start == -1 else line_start + 1
            line = text[line_start:i + 1].strip()

            if line.startswith("#"):
                return skip_leading_whitespace(text, i + 1)

        i -= 1

    return 0


def skip_leading_whitespace(text: str, index: int) -> int:
    i = index
    while i < len(text) and text[i].isspace():
        i += 1
    return i


def parse_parameter_type(param: str) -> str:
    param = strip_comments(param).strip()

    if not param:
        return ""

    if param == "void":
        return "void"

    param = remove_default_value(param)
    param = normalize_whitespace(param)

    # Function pointer or complex parameter: keep normalized full expression.
    if "(" in param and ")" in param and "*" in param:
        return normalize_type(param)

    # Remove common Unreal/C++ parameter annotations if they appear as standalone tokens.
    annotation_keywords = {
        "UPARAM",
    }

    for keyword in annotation_keywords:
        param = re.sub(rf"\b{keyword}\s*\([^)]*\)", "", param)

    param = normalize_whitespace(param)

    # Try to remove the parameter name.
    #
    # Examples:
    #   "const FViewInfo& View" -> "const FViewInfo&"
    #   "FRDGBuilder& GraphBuilder" -> "FRDGBuilder&"
    #   "const FGlobalShaderMap* ShaderMap" -> "const FGlobalShaderMap*"
    #   "TArray<FVector>& OutValues" -> "TArray<FVector>&"
    #
    # If no obvious parameter name exists, keep the full normalized text.
    match = re.match(
        r"^(?P<type>.+?)(?:\s+|\s*)(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
        r"(?P<array>(?:\s*\[[^\]]*\])*)$",
        param,
    )

    if match:
        type_part = match.group("type").strip()
        name_part = match.group("name").strip()

        reserved = {
            "const",
            "volatile",
            "unsigned",
            "signed",
            "short",
            "long",
            "int",
            "float",
            "double",
            "char",
            "bool",
            "void",
            "auto",
        }

        if name_part not in reserved:
            return normalize_type(type_part + match.group("array"))

    return normalize_type(param)


def parse_signature(signature_text: str) -> FunctionSignature:
    text = strip_comments(signature_text)
    text = normalize_whitespace(text)

    name_match = re.search(r"([~A-Za-z_][A-Za-z0-9_:]*)\s*\(", text)
    if not name_match:
        raise RuntimeError(f"Failed to parse function name from signature:\n{signature_text}")

    full_name = name_match.group(1)
    function_name = full_name.split("::")[-1]

    open_paren = text.find("(", name_match.start())
    close_paren = find_matching_paren(text, open_paren)
    if close_paren is None:
        raise RuntimeError(f"Failed to parse parameter list from signature:\n{signature_text}")

    return_type = text[:name_match.start()].strip()
    if not return_type:
        raise RuntimeError(f"Failed to parse return type from signature:\n{signature_text}")

    param_text = text[open_paren + 1:close_paren].strip()
    raw_params = split_top_level_commas(param_text)

    parameter_types = []
    for param in raw_params:
        parsed = parse_parameter_type(param)
        if parsed:
            parameter_types.append(parsed)

    return FunctionSignature(
        return_type=normalize_type(return_type),
        function_name=function_name,
        parameter_types=parameter_types,
    )


def parse_candidate_header(raw_header: str) -> Optional[FunctionSignature]:
    header = strip_comments(raw_header)
    header = normalize_whitespace(header)

    name_match = re.search(r"([~A-Za-z_][A-Za-z0-9_:]*)\s*\(", header)
    if not name_match:
        return None

    full_name = name_match.group(1)
    function_name = full_name.split("::")[-1]

    open_paren = header.find("(", name_match.start())
    close_paren = find_matching_paren(header, open_paren)
    if close_paren is None:
        return None

    return_type = header[:name_match.start()].strip()
    if not return_type:
        return None

    param_text = header[open_paren + 1:close_paren].strip()
    raw_params = split_top_level_commas(param_text)

    parameter_types = []
    for param in raw_params:
        parsed = parse_parameter_type(param)
        if parsed:
            parameter_types.append(parsed)

    return FunctionSignature(
        return_type=normalize_type(return_type),
        function_name=function_name,
        parameter_types=parameter_types,
    )


def collect_function_candidates(text: str, function_name: str) -> list[FunctionCandidate]:
    debug(f"Collecting function candidates: {function_name}")

    candidates: list[FunctionCandidate] = []
    pattern = re.compile(r"\b" + re.escape(function_name) + r"\s*\(")

    for match_index, match in enumerate(pattern.finditer(text), start=1):
        debug(f"Name match #{match_index}: index={match.start()}")

        open_paren = text.find("(", match.start())
        if open_paren < 0:
            continue

        close_paren = find_matching_paren(text, open_paren)
        if close_paren is None:
            debug("Skipped: no matching close paren")
            continue

        header_end = skip_function_suffix(text, close_paren + 1)

        if header_end >= len(text) or text[header_end] != "{":
            debug(f"Skipped: not a function definition near index={match.start()}")
            continue

        brace_end = find_matching_brace(text, header_end)
        if brace_end is None:
            debug(f"Skipped: no matching closing brace near index={match.start()}")
            continue

        start = find_header_start(text, match.start())
        raw_header = text[start:header_end].strip()

        parsed = parse_candidate_header(raw_header)
        if parsed is None:
            debug(f"Skipped: failed to parse header:\n{raw_header}")
            continue

        if parsed.function_name != function_name:
            debug(f"Skipped: parsed function mismatch: {parsed.function_name}")
            continue

        candidate = FunctionCandidate(
            start_index=start,
            header_end_index=header_end,
            body_end_index=brace_end + 1,
            raw_header=raw_header,
            return_type=parsed.return_type,
            function_name=parsed.function_name,
            parameter_types=parsed.parameter_types,
        )

        candidates.append(candidate)

        debug("Accepted candidate:")
        debug(f"  header     : {candidate.raw_header}")
        debug(f"  return     : {candidate.return_type}")
        debug(f"  parameters : {candidate.parameter_types}")
        debug(f"  range      : {candidate.start_index} - {candidate.body_end_index}")

    debug(f"Collected candidates: {len(candidates)}")
    return candidates


def format_candidates(candidates: list[FunctionCandidate]) -> str:
    if not candidates:
        return "(none)"

    lines: list[str] = []

    for index, candidate in enumerate(candidates, start=1):
        lines.append(f"[{index}] {candidate.raw_header}")
        lines.append(f"    return     : {candidate.return_type}")
        lines.append(f"    parameters : {candidate.parameter_types}")
        lines.append(f"    range      : {candidate.start_index} - {candidate.body_end_index}")

    return "\n".join(lines)


def select_candidate(
    candidates: list[FunctionCandidate],
    expected: FunctionSignature,
    context_label: str,
) -> FunctionCandidate:
    info(f"Searching target function in {context_label}")
    info(f"Requested declaration:")
    info(f"{expected.return_type} {expected.function_name}({', '.join(expected.parameter_types)})")

    if not candidates:
        raise RuntimeError(
            f"No function definitions with name '{expected.function_name}' were found in {context_label}."
        )

    info(f"Same-name function definitions found: {len(candidates)}")

    same_return = [
        candidate for candidate in candidates
        if candidate.return_type == expected.return_type
    ]

    info(f"Candidates after return type filter: {len(same_return)}")

    if not same_return:
        raise RuntimeError(
            "No function matched the requested return type.\n"
            f"Requested return type: {expected.return_type}\n"
            f"Discovered declarations in {context_label}:\n{format_candidates(candidates)}"
        )

    same_parameters = [
        candidate for candidate in same_return
        if candidate.parameter_types == expected.parameter_types
    ]

    info(f"Candidates after parameter structure filter: {len(same_parameters)}")

    if len(same_parameters) == 1:
        info("Matched declaration:")
        info(same_parameters[0].raw_header)
        return same_parameters[0]

    if len(same_parameters) == 0:
        raise RuntimeError(
            "No function matched the requested parameter structure.\n"
            f"Requested return type : {expected.return_type}\n"
            f"Requested parameters  : {expected.parameter_types}\n"
            f"Discovered declarations after return type filtering in {context_label}:\n"
            f"{format_candidates(same_return)}"
        )

    raise RuntimeError(
        "Multiple functions matched the requested return type and parameter structure.\n"
        f"Requested return type : {expected.return_type}\n"
        f"Requested parameters  : {expected.parameter_types}\n"
        f"Discovered declarations in {context_label}:\n"
        f"{format_candidates(same_parameters)}"
    )


def replace_function(edited_path: Path, target_path: Path, signature_text: str) -> None:
    expected = parse_signature(signature_text)

    info("Function replacement started.")
    info(f"Edited file: {edited_path}")
    info(f"Target file: {target_path}")

    edited_text = edited_path.read_text(encoding="utf-8")
    target_text = target_path.read_text(encoding="utf-8")

    edited_candidates = collect_function_candidates(edited_text, expected.function_name)
    edited_candidate = select_candidate(
        edited_candidates,
        expected,
        f"edited file: {edited_path}",
    )

    target_candidates = collect_function_candidates(target_text, expected.function_name)
    target_candidate = select_candidate(
        target_candidates,
        expected,
        f"target file: {target_path}",
    )

    replacement = edited_text[edited_candidate.start_index:edited_candidate.body_end_index].strip() + "\n"

    new_text = (
        target_text[:target_candidate.start_index]
        + replacement
        + target_text[target_candidate.body_end_index:]
    )

    debug("Replacement range:")
    debug(f"  edited start : {edited_candidate.start_index}")
    debug(f"  edited end   : {edited_candidate.body_end_index}")
    debug(f"  target start : {target_candidate.start_index}")
    debug(f"  target end   : {target_candidate.body_end_index}")
    debug(f"  old length   : {len(target_text)}")
    debug(f"  new length   : {len(new_text)}")

    if new_text == target_text:
        info("Target file content is unchanged after replacement.")

    target_path.write_text(new_text, encoding="utf-8")

    info("Function replacement completed successfully.")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--edited", required=True)
    parser.add_argument("--target", required=True)
    parser.add_argument("--signature", required=True)
    parser.add_argument("--debug", action="store_true")
    return parser.parse_args()


def main() -> int:
    global DEBUG

    args = parse_args()
    DEBUG = DEBUG or args.debug

    edited_path = Path(args.edited)
    target_path = Path(args.target)
    signature_text = args.signature

    if not edited_path.is_file():
        error(f"Edited file not found: {edited_path}")
        return 1

    if not target_path.is_file():
        error(f"Target file not found: {target_path}")
        return 1

    try:
        replace_function(edited_path, target_path, signature_text)
    except Exception as exc:
        error(str(exc))
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())