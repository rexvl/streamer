async function loadStreams()
{
    const tbody = document.getElementById("streams");

    tbody.innerHTML = "";


    try
    {
        const [videoList, audioList, streams] = await Promise.all([
            fetch('/devices/video').then(r => r.json()),
            fetch('/devices/audio').then(r => r.json()),
            fetch('/streams').then(r => r.json())
        ]);

        // build id->name maps
        const videoMap = {};
        for (const d of videoList) {
            if (d.id) videoMap[d.id] = d.name || d.id;
        }

        const audioMap = {};
        for (const d of audioList) {
            if (d.id) audioMap[d.id] = d.name || d.id;
        }
        // populate device selects
        populateDeviceSelect('video-select', videoList);
        populateDeviceSelect('audio-select', audioList);
        // show/hide audio and video params depending on selected device
        updateAudioParamsVisibility();
        updateVideoParamsVisibility();
        // show/hide video params depending on selected device
        updateVideoParamsVisibility();

        for (const stream of streams)
        {
            addStreamRow(stream, videoMap, audioMap);
        }

    }
    catch (e)
    {
        tbody.innerHTML =
        `<tr>
                <td colspan="4">
                    Error: ${e}
                </td>
             </tr>`;
    }
}



function addStreamRow(stream, videoMap, audioMap)
{
    const tbody = document.getElementById("streams");


    const row = document.createElement("tr");


    let outputs = "";

    for (const output of stream.outputs ?? [])
    {
        outputs += `
            ${output.url}
            <br>
        `;
    }


    row.innerHTML = `

        
        <td>
            ${
                stream.video
                ?
                `
                ${videoMap && videoMap[stream.video.device] ? videoMap[stream.video.device] : stream.video.device}
                <br>
                ${stream.video.codec || ''} ${stream.video.width ? (' - ' + stream.video.width + 'x' + stream.video.height) : ''}
                ${stream.video.fps_n ? (' - ' + stream.video.fps_n + '/' + stream.video.fps_d + ' fps') : ''}
                ${stream.video.bitrate ? (' - ' + stream.video.bitrate + ' kbps') : ''}
                `
                :
                "disabled"
            }
        </td>


        <td>
            ${
                stream.audio
                ?
                `
                ${audioMap && audioMap[stream.audio.device] ? audioMap[stream.audio.device] : stream.audio.device}
                <br>
                ${stream.audio.codec || ''} ${stream.audio.sampleRate ? (' - ' + stream.audio.sampleRate + ' Hz') : ''}
                ${stream.audio.channels ? (' - ' + stream.audio.channels + ' ch') : ''}
                ${stream.audio.bitrate ? (' - ' + Math.round(stream.audio.bitrate / 1000) + ' kbps') : ''}
                `
                :
                "disabled"
            }
        </td>


        <td>
            ${outputs || "-"}
        </td>


        <td>
            <button
                class="btn"
                onclick="editStream('${stream.id}')">
                Edit
            </button>
            <button
                class="danger"
                onclick="deleteStream('${stream.id}')"
                style="margin-left:8px">
                Delete
            </button>
        </td>

    `;


    tbody.appendChild(row);
}



async function deleteStream(id)
{
    if (!confirm(
        "Delete stream " + id + "?"
    ))
        return;


    await fetch(
        "/streams/" + id,
        {
            method: "DELETE"
        }
    );


    loadStreams();
}
// Expose functions to global window for inline onclick handlers
// (some environments may not bind function declarations to window automatically)
window.editStream = typeof editStream !== 'undefined' ? editStream : undefined;
window.cancelEdit = typeof cancelEdit !== 'undefined' ? cancelEdit : undefined;
window.deleteStream = typeof deleteStream !== 'undefined' ? deleteStream : undefined;
window.addOutputRow = typeof addOutputRow !== 'undefined' ? addOutputRow : undefined;
window.removeOutputRow = typeof removeOutputRow !== 'undefined' ? removeOutputRow : undefined;
window.createStream = typeof createStream !== 'undefined' ? createStream : undefined;

let currentEditingId = null;

async function editStream(id) {
    try {
        const resp = await fetch('/streams/' + id);
        if (resp.status !== 200) {
            alert('Failed to load stream for edit');
            return;
        }

        const stream = await resp.json();

        // fill form
        currentEditingId = id;

        // video
        const videoSel = document.getElementById('video-select');
        if (videoSel) videoSel.value = stream.video ? stream.video.device : '';
        updateVideoParamsVisibility();
        if (stream.video) {
            const wEl = document.getElementById('video-width');
            const hEl = document.getElementById('video-height');
            const fpsEl = document.getElementById('video-fps');
            const vcodecEl = document.getElementById('video-codec');
            const vbEl = document.getElementById('video-bitrate');

            if (wEl) wEl.value = stream.video.width || '';
            if (hEl) hEl.value = stream.video.height || '';
            if (vcodecEl) vcodecEl.value = stream.video.codec || 'avc';
            if (vbEl) vbEl.value = stream.video.bitrate || '';

            if (fpsEl) {
                if (stream.video.fps_n && stream.video.fps_d) {
                    if (stream.video.fps_d === 1) fpsEl.value = String(stream.video.fps_n);
                    else fpsEl.value = stream.video.fps_n + '/' + stream.video.fps_d;
                } else {
                    fpsEl.value = '';
                }
            }
        }

        // audio
        const audioSel = document.getElementById('audio-select');
        if (audioSel) audioSel.value = stream.audio ? stream.audio.device : '';
        updateAudioParamsVisibility();
        if (stream.audio) {
            const srEl = document.getElementById('audio-samplerate');
            const chEl = document.getElementById('audio-channels');
            const acodecEl = document.getElementById('audio-codec');
            const abrEl = document.getElementById('audio-bitrate');

            if (srEl) srEl.value = stream.audio.sampleRate || srEl.value;
            if (chEl) chEl.value = stream.audio.channels || chEl.value;
            if (acodecEl) acodecEl.value = stream.audio.codec || acodecEl.value;
            // server stores bitrate in bits/sec (e.g. 128000) — convert to kbps for UI
            if (abrEl) abrEl.value = stream.audio.bitrate ? Math.round(stream.audio.bitrate / 1000) : abrEl.value;
        }

        // outputs
        const container = document.getElementById('outputs-container');
        if (container) {
            container.innerHTML = '';
            const outs = stream.outputs || [];
            if (outs.length === 0) {
                addOutputRow();
            } else {
                for (const o of outs) {
                    const row = document.createElement('div');
                    row.className = 'output-row';
                    row.innerHTML = `
                        <select class="output-type">
                            <option value="rtsp">RTSP</option>
                            <option value="rtmp">RTMP</option>
                        </select>
                        <input class="output-url" type="text" placeholder="url (e.g. rtmp://...)">
                        <button type="button" class="btn small" onclick="removeOutputRow(this)">Remove</button>
                    `;
                    container.appendChild(row);
                    const typeEl = row.querySelector('.output-type');
                    const urlEl = row.querySelector('.output-url');
                    if (typeEl) typeEl.value = o.type || 'rtsp';
                    if (urlEl) urlEl.value = o.url || '';
                }
            }
        }

        // set UI to edit mode
        const createBtn = document.getElementById('create-btn');
        const cancelBtn = document.getElementById('cancel-edit-btn');
        if (createBtn) createBtn.textContent = 'Save';
        if (cancelBtn) cancelBtn.style.display = '';

        // scroll to form
        document.getElementById('new-stream-section').scrollIntoView({ behavior: 'smooth' });

    } catch (e) {
        alert('Failed to load stream: ' + e);
    }
}

function cancelEdit() {
    currentEditingId = null;
    // reset form fields
    const form = document.getElementById('new-stream-form');
    if (form) form.reset();
    // reset selects to (none)
    const videoSel = document.getElementById('video-select');
    const audioSel = document.getElementById('audio-select');
    if (videoSel) videoSel.value = '';
    if (audioSel) audioSel.value = '';
    updateAudioParamsVisibility();
    updateVideoParamsVisibility();
    // reset outputs to single empty row
    const container = document.getElementById('outputs-container');
    if (container) {
        container.innerHTML = '';
        addOutputRow();
    }
    const createBtn = document.getElementById('create-btn');
    const cancelBtn = document.getElementById('cancel-edit-btn');
    if (createBtn) createBtn.textContent = 'Create stream';
    if (cancelBtn) cancelBtn.style.display = 'none';
}



function populateDeviceSelect(selectId, deviceList) {
    const sel = document.getElementById(selectId);
    if (!sel) return;

    // remove existing options except the first (none)
    while (sel.options.length > 1) sel.remove(1);

    for (const d of deviceList) {
        const opt = document.createElement('option');
        opt.value = d.id || '';
        opt.textContent = d.name || d.id || '';
        sel.appendChild(opt);
    }
}


function updateAudioParamsVisibility() {
    const audioSel = document.getElementById('audio-select');
    const params = document.querySelector('.audio-params');
    if (!params || !audioSel) return;

    if (!audioSel.value) {
        params.style.display = 'none';
    } else {
        // ensure grid display
        params.style.display = 'grid';
    }
}

function updateVideoParamsVisibility() {
    const videoSel = document.getElementById('video-select');
    const params = document.querySelector('.video-params');
    if (!params || !videoSel) return;

    if (!videoSel.value) {
        params.style.display = 'none';
    } else {
        params.style.display = 'grid';
    }
}


async function createStream() {
    const videoSel = document.getElementById('video-select');
    const audioSel = document.getElementById('audio-select');
    const body = {};
    if (videoSel && videoSel.value) {
        const vcodecEl = document.getElementById('video-codec');
        const wEl = document.getElementById('video-width');
        const hEl = document.getElementById('video-height');
        const fpsEl = document.getElementById('video-fps');
        const vbEl = document.getElementById('video-bitrate');

        const videoObj = {
            device: videoSel.value,
            codec: vcodecEl ? vcodecEl.value : 'avc'
        };

        const w = wEl ? Number(wEl.value) : 0;
        const h = hEl ? Number(hEl.value) : 0;
        const vb = vbEl ? Number(vbEl.value) : 0;

        if (w > 0) videoObj.width = w;
        if (h > 0) videoObj.height = h;

        // parse FPS: allow "30" or "30000/1001" or "30/1"
        if (fpsEl && fpsEl.value) {
            const v = fpsEl.value.trim();
            if (v.includes('/')) {
                const parts = v.split('/');
                const n = Number(parts[0]) || 0;
                const d = Number(parts[1]) || 1;
                if (n > 0) {
                    videoObj.fps_n = n;
                    videoObj.fps_d = d > 0 ? d : 1;
                }
            } else {
                const n = Number(v) || 0;
                if (n > 0) {
                    videoObj.fps_n = n;
                    videoObj.fps_d = 1;
                }
            }
        }

        if (vb > 0) videoObj.bitrate = vb;

        body.video = videoObj;
    }

    if (audioSel && audioSel.value) {
        const codecEl = document.getElementById('audio-codec');
        const srEl = document.getElementById('audio-samplerate');
        const chEl = document.getElementById('audio-channels');
        const brEl = document.getElementById('audio-bitrate');

        const audioObj = {
            device: audioSel.value,
            codec: codecEl ? codecEl.value : 'aac'
        };

        const sr = srEl ? Number(srEl.value) : 0;
        const ch = chEl ? Number(chEl.value) : 0;
        const br = brEl ? Number(brEl.value) : 0; // br is in kbps from UI

        if (sr > 0) audioObj.sampleRate = sr;
        if (ch > 0) audioObj.channels = ch;
        // send bitrate in bits/sec (e.g. 128 kbps -> 128000)
        if (br > 0) audioObj.bitrate = br * 1000;

        body.audio = audioObj;
    }

    // must specify at least video or audio
    if (!body.video && !body.audio) {
        alert('Please specify a video or audio device (at least one).');
        return;
    }

    // collect outputs from form — do not allow empty output rows
    body.outputs = [];
    const rows = document.querySelectorAll('#outputs-container .output-row');
    if (!rows || rows.length === 0) {
        alert('Please add at least one output with a URL.');
        return;
    }

    for (const r of rows) {
        const typeEl = r.querySelector('.output-type');
        const urlEl = r.querySelector('.output-url');
        if (!typeEl || !urlEl) continue;
        const type = typeEl.value;
        const url = urlEl.value.trim();
        if (url.length === 0) {
            alert('Please fill URL for all outputs or remove empty rows.');
            return;
        }
        body.outputs.push({ type: type, url: url });
    }

    // ensure outputs are unique within this new stream
    const seen = new Set();
    for (const o of body.outputs) {
        if (seen.has(o.url)) {
            alert('Duplicate outputs in the form: output URL "' + o.url + '" appears multiple times.');
            return;
        }
        seen.add(o.url);
    }

    // ensure no output URL duplicates an existing stream's outputs
    try {
        const existingStreams = await fetch('/streams').then(r => r.json());
        const existingUrls = new Set();
        for (const s of existingStreams || []) {
            // when editing, allow outputs that belong to the stream being edited
            if (currentEditingId && s.id === currentEditingId) continue;
            for (const o of s.outputs || []) {
                if (o && o.url) existingUrls.add(o.url);
            }
        }

        for (const o of body.outputs) {
            if (existingUrls.has(o.url)) {
                alert('Output URL "' + o.url + '" is already used by another stream. Choose a different URL.');
                return;
            }
        }
    } catch (e) {
        // if checking existing streams fails, block creation and inform user
        alert('Failed to check existing streams: ' + e);
        return;
    }

    try {
        let resp;
        if (currentEditingId) {
            resp = await fetch('/streams/' + currentEditingId, {
                method: 'PUT',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(body)
            });
        } else {
            resp = await fetch('/streams', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(body)
            });
        }

        if (resp.status === 201 || resp.status === 200) {
            alert(currentEditingId ? 'Stream updated' : 'Stream created');
            currentEditingId = null;
            const createBtn = document.getElementById('create-btn');
            const cancelBtn = document.getElementById('cancel-edit-btn');
            if (createBtn) createBtn.textContent = 'Create stream';
            if (cancelBtn) cancelBtn.style.display = 'none';
            loadStreams();
        } else {
            const j = await resp.json().catch(() => null);
            alert('Failed to create/update stream: ' + (j && j.error ? j.error : resp.statusText));
        }
    } catch (e) {
        alert('Error: ' + e);
    }
}

function addOutputRow() {
    const container = document.getElementById('outputs-container');
    if (!container) return;

    const row = document.createElement('div');
    row.className = 'output-row';
    row.innerHTML = `
        <select class="output-type">
            <option value="rtsp">RTSP</option>
            <option value="rtmp">RTMP</option>
        </select>
        <input class="output-url" type="text" placeholder="url (e.g. rtmp://...)">
        <button type="button" class="btn small" onclick="removeOutputRow(this)">Remove</button>
    `;

    container.appendChild(row);
}

function removeOutputRow(btn) {
    const container = document.getElementById('outputs-container');
    if (!container) return;

    const rows = container.querySelectorAll('.output-row');
    if (!rows || rows.length <= 1) {
        alert('Cannot remove the last output. Add another output first.');
        return;
    }

    const row = btn && btn.parentElement;
    if (!row) return;
    container.removeChild(row);
}

// attach change listener so visibility updates when user changes audio device
const audioSelectEl = document.getElementById && document.getElementById('audio-select');
if (audioSelectEl) {
    audioSelectEl.addEventListener('change', updateAudioParamsVisibility);
}

// attach listener for video select
const videoSelectEl = document.getElementById && document.getElementById('video-select');
if (videoSelectEl) {
    videoSelectEl.addEventListener('change', updateVideoParamsVisibility);
}

// Fallback: listen for change events on the document in case inline handlers/listeners
// don't fire in some environments. This ensures visibility toggles whenever the
// selects change value.
document.addEventListener('change', function (ev) {
    const t = ev.target;
    if (!t) return;
    if (t.id === 'video-select') updateVideoParamsVisibility();
    if (t.id === 'audio-select') updateAudioParamsVisibility();
});

loadStreams();
