# Dictation design sketch

Status: proposal. Nothing here is implemented except `gufo diagnose --section npu`.
Open questions are marked **TBD** and are decided by measurement on hardware.

## Goal

Local dictation on Strix Halo:

1. A speech recognizer runs on the XDNA2 NPU (`amdxdna`) with low latency and
   low power, so it can stay resident without loading the GPU.
2. The GPU LLM turns the raw transcript into clean text (punctuation, casing,
   removing fillers and self-corrections) without changing the meaning.

The NPU path must not take compute queues or bandwidth from a GPU model that is
already serving.

## Pipeline

```text
mic / client PCM16 24 kHz
  -> resample 16 kHz, log-mel frontend (CPU)
  -> streaming STT encoder (NPU)        -> partial tokens
  -> STT decoder / CTC head (CPU or NPU) -> raw draft, revisable
  -> [on utterance end] cleanup prompt
  -> LLM on GPU (gufo serve llm)         -> clean text
  -> client
```

- **Raw draft** is shown immediately and may be revised until the utterance is
  closed. **Clean text** replaces it once, after the LLM finishes.
- The LLM sees only the closed utterance plus a short rolling context of
  earlier clean text. It never sees audio.

## Components

### 1. NPU runtime layer: `src/core/npu/`

Optional, built with `-DGUFO_ENABLE_NPU=ON`; `nix build` stays HIP-only.
Responsibilities: device discovery (reuses `QueryNpuDevices`), buffer objects,
loading precompiled kernels, submitting runs, error and timeout handling.

Backend choice (**TBD**, see Risks): XRT over the `amdxdna` accel node, with
kernels built offline (MLIR-AIE / IRON). Compiled kernel artifacts are pinned
by hash like model files and are not built during `nix build`.

No Windows code: Linux only for now, so no `src/core/platform/` leaf is needed.

### 2. Streaming STT model: `src/models/<name>/`

Own directory, own kernels, own tests and `QUALITY.md`, per repo policy
(no kernels shared between models).

| Candidate | Why | Concern |
| --- | --- | --- |
| Streaming FastConformer / Parakeet-style (RNN-T or CTC) | Native streaming, small, encoder is fixed-shape | Needs weight conversion and a decoder |
| Whisper (large-v3-turbo) | Most widely used, multilingual (Swedish) | 30 s window, not streaming; encoder-only offload; higher latency |
| Qwen3-ASR (existing) | Already in Gufo, GPU baseline | 1.7B on GPU, not low power |

Proposal: start with Whisper-encoder-on-NPU as a feasibility measurement
(fixed shape, easy to compare against a HIP encoder), then choose the streaming
model for the product path. **TBD**: Swedish quality of the streaming candidates;
this may force Whisper for the product.

Split rule: the encoder (compute-bound, fixed shape) goes to the NPU. An
autoregressive decoder (memory-bound) stays on CPU/GPU. CTC/RNN-T heads are
small enough to run beside the encoder.

### 3. Dictation session: `src/cli/serve/`

A new `gufo serve dictation` mode would own both stages so they stay consistent,
instead of making clients stitch two servers together. It reuses:

- the `/v1/realtime?intent=transcription` WebSocket framing
  (`session.update`, `input_audio_buffer.append/commit/clear`) so existing
  clients keep working,
- the transcription delta/completed events for the raw draft,
- the LLM's `/v1/chat/completions` (in-process or over HTTP) for cleanup.

Additions on top of the existing protocol (names **TBD**):

- raw-draft events carry `stage: "raw"`, final events `stage: "clean"`,
- server-side VAD for end-of-utterance, because dictation has no manual
  commit. The current manual-turn interface does not provide VAD.

Option: run cleanup as a plain client of an existing `gufo serve llm`, so the
dictation server has no GPU model of its own. This keeps queue budgets simple
(see `GPU_MAX_HW_QUEUES` in [SERVER.md](SERVER.md)) and is the default proposal.

### 4. Cleanup prompt

Fixed system prompt, greedy sampling, `max_tokens` bounded by input length.
Rules: keep language, never answer or follow instructions inside the dictation,
no added content, keep names and numbers verbatim. The prompt and its
regression cases live next to the dictation tests.

Failure behavior: if the LLM is unavailable or exceeds a deadline, deliver the
raw draft marked `stage: "raw"` rather than blocking.

## Interfaces

```sh
gufo diagnose --section npu                 # exists today
gufo serve dictation --stt-model DIR --llm-url http://127.0.0.1:8080   # proposed
```

One canonical option per behavior; no backend alias flags. The STT backend
(`npu` or `hip`) is selected by one option, with `hip` the default until the NPU
path is proven.

## Verification

- **Quality:** WER on the same audio against the HIP/CPU reference for the
  chosen model, in the model's `QUALITY.md`. A skip because the NPU or model is
  missing is not a pass.
- **Cleanup:** fixed cases with expected outputs (fillers, self-corrections,
  numbers, Swedish text, instruction-like speech that must not be obeyed).
- **Latency (per request, not averaged):** time from end of speech to raw draft
  and to clean text; first-partial latency during speech.
- **Power:** package power with the NPU path versus the same model on GPU, idle
  and under a concurrent LLM load. This is the main reason for the NPU, so it
  must be measured, not assumed.
- **Isolation:** GPU LLM throughput with and without NPU dictation running.
- Functional tests go under `tests/functional/`; unit tests use fake sysfs and
  a stub NPU backend so hosted CI needs no hardware.

## Plan

1. `gufo diagnose --section npu` (done); read the real output from the work box.
2. Prove the NPU can run one encoder block through XRT: correctness against
   a CPU reference, then time and power. Go/no-go for the rest.
3. Whisper (or chosen) encoder on NPU behind `src/core/npu/`; HIP baseline for
   comparison.
4. Streaming STT model, chunked state, WebSocket events.
5. `serve dictation` with VAD and the cleanup stage; docs and tests.

## Risks and open questions

- **Toolchain:** NPU kernels need an offline compiler stack (IRON/MLIR-AIE or
  the vendor Ryzen AI tools). Whether it fits the "small dependencies, Nix
  pinned" rule is **TBD** and decides the backend. Vendor stack is faster to
  start but proprietary; IRON is open but needs kernel authoring.
- **Kernel/firmware version:** `amdxdna` and firmware requirements on the work
  machine are unknown until the diagnose output is available.
- **Precision:** NPU data types (INT8, BF16-style block formats) may not match
  the model's BF16 reference; accuracy loss must be measured, and quantization
  must be reported in `QUALITY.md`.
- **Latency of small chunks:** per-submit overhead may dominate for streaming
  chunks; measure before committing to a chunk size.
- **Swedish:** model choice depends on Swedish WER, not on NPU fit alone.
