"""Inpainting helpers for background tiles: upload init+mask to ComfyUI, run the
masked krea2 repaint, and composite so pixels outside the mask stay exact."""

import io
import json
import time
import urllib.request
import uuid
from pathlib import Path

import numpy as np
from PIL import Image

import spritegen

HERE = Path(__file__).resolve().parent


def upload_image(img, name):
    """POST a PIL image to ComfyUI's input store."""
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    data = buf.getvalue()
    boundary = uuid.uuid4().hex
    body = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="image"; filename="{name}"\r\n'
        f"Content-Type: image/png\r\n\r\n"
    ).encode() + data + (
        f"\r\n--{boundary}\r\n"
        f'Content-Disposition: form-data; name="overwrite"\r\n\r\ntrue\r\n'
        f"--{boundary}--\r\n"
    ).encode()
    req = urllib.request.Request(
        spritegen.SERVER + "/upload/image",
        data=body,
        headers={"Content-Type": f"multipart/form-data; boundary={boundary}"},
    )
    with urllib.request.urlopen(req, timeout=120) as r:
        return json.loads(r.read())["name"]


def img2img_window(init, prompt, seed, denoise=0.55, tag="i2i"):
    """Re-render the WHOLE window at moderate denoise (model sees full context in
    pixel space — no latent masking). Caller composites the region it wants."""
    full = Image.new("L", init.size, 255)
    return inpaint(init, full, prompt, seed, denoise=denoise, tag=tag, composite=False)


def inpaint(init, mask, prompt, seed, denoise=0.7, tag="inpaint", composite=True):
    """Repaint the white region of `mask` in `init`; everything else is composited
    back exactly from the init afterwards. Returns RGB PIL at init size."""
    nonce = f"{int(time.time())}_{seed}_{tag}"
    init_name = upload_image(init.convert("RGB"), f"ab_init_{nonce}.png")
    mask_name = upload_image(mask.convert("RGB"), f"ab_mask_{nonce}.png")
    wf = json.loads((HERE / "pixelart_inpaint_api.json").read_text())
    wf.pop("_meta_comment", None)
    wf["6"]["inputs"]["text"] = prompt
    wf["40"]["inputs"]["image"] = init_name
    wf["41"]["inputs"]["image"] = mask_name
    wf["3"]["inputs"]["seed"] = seed
    wf["3"]["inputs"]["denoise"] = denoise
    wf["30"]["inputs"]["filename_prefix"] = f"ab_out_{nonce}"
    outs = spritegen.run_workflow(wf)
    result = (outs.get("30") or next(iter(outs.values()))).convert("RGB")
    if result.size != init.size:
        result = result.resize(init.size, Image.NEAREST)
    if not composite:
        return result
    a_init = np.asarray(init.convert("RGB"))
    a_res = np.asarray(result)
    m = np.asarray(mask.convert("L")) > 127
    # crisp 6px dither zone at the mask boundary so the handoff isn't a hard line
    from scipy.ndimage import binary_dilation, binary_erosion
    inner = binary_erosion(m, iterations=6)
    fringe = m & ~inner
    rng = np.random.default_rng(seed)
    keep_res = inner | (fringe & (rng.random(m.shape) < 0.5))
    out = np.where(keep_res[..., None], a_res, a_init)
    return Image.fromarray(out.astype(np.uint8), "RGB")
