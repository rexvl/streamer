async function loadStreams()
{
    const tbody = document.getElementById("streams");

    tbody.innerHTML = "";


    try
    {
        const response = await fetch("/streams");

        const streams = await response.json();


        for (const stream of streams)
        {
            addStreamRow(stream);
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



function addStreamRow(stream)
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
                stream.audio
                ?
                `
                ${stream.audio.device}
                <br>
                ${stream.audio.sampleRate} Hz
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