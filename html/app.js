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
                ${stream.audio.bitrate ? (' - ' + stream.audio.bitrate + ' kbps') : ''}
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
                class="danger"
                onclick="deleteStream('${stream.id}')">
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
        const br = brEl ? Number(brEl.value) : 0;

        if (sr > 0) audioObj.sampleRate = sr;
        if (ch > 0) audioObj.channels = ch;
        if (br > 0) audioObj.bitrate = br;

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
        const resp = await fetch('/streams', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(body)
        });

        if (resp.status === 201) {
            alert('Stream created');
            loadStreams();
        } else {
            const j = await resp.json().catch(() => null);
            alert('Failed to create stream: ' + (j && j.error ? j.error : resp.statusText));
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
