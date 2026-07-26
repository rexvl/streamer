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

        for (const stream of streams)
        {
            addStreamRow(stream, videoMap, audioMap);
        }

    }
    catch (e)
    {
        tbody.innerHTML =
        `<tr>
                <td colspan="5">
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
            ${output.type}:
            ${output.url}
            <br>
        `;
    }


    row.innerHTML = `

        <td>
            ${stream.id}
        </td>


        <td>
            ${
                stream.video
                ?
                `
                ${videoMap && videoMap[stream.video.device] ? videoMap[stream.video.device] : stream.video.device}
                <br>
                ${stream.video.width || ''}x${stream.video.height || ''}
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
                ${stream.audio.sampleRate || ''} Hz
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


async function createStream() {
    const videoSel = document.getElementById('video-select');
    const audioSel = document.getElementById('audio-select');
    const body = {};
    if (videoSel && videoSel.value) {
        body.video = {
            device: videoSel.value,
            codec: 'avc'
        };
    }

    if (audioSel && audioSel.value) {
        body.audio = {
            device: audioSel.value,
            codec: 'aac'
        };
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
    const row = btn && btn.parentElement;
    if (!row) return;
    const container = document.getElementById('outputs-container');
    if (!container) return;
    container.removeChild(row);
}

loadStreams();
