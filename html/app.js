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



loadStreams();