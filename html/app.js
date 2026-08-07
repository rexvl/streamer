/* app.js - jQuery UI integrated with streamer REST API */

$(function(){
  const POLL_MS = 2000;

  let streamsMap = {}; // id -> StreamSettings
  let statusMap = {};  // id -> StreamStatus
  let videoDevices = [];
  let audioDevices = [];
  let activeStreamId = null;

  async function apiGet(path) {
    const r = await fetch(path);
    if (!r.ok) throw new Error(`GET ${path} failed: ${r.status}`);
    return r.json();
  }

  function sourceStatusToClass(s) {
    // s expected to be string: 'success','unavailable','fail','disabled','unknown'
    if (!s) return 'status-stopped';
    if (s === 'success') return 'status-live';
    if (s === 'unavailable') return 'status-starting';
    if (s === 'fail') return 'status-error';
    if (s === 'disabled') return 'status-stopped';
    return 'status-stopped';
  }

  async function apiPost(path, body) {
    const r = await fetch(path, { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify(body) });
    return r;
  }

  async function apiPut(path, body) {
    const r = await fetch(path, { method: 'PUT', headers: {'Content-Type':'application/json'}, body: JSON.stringify(body) });
    return r;
  }

  async function apiDelete(path) {
    const r = await fetch(path, { method: 'DELETE' });
    return r;
  }

  async function refreshAll(){
    try {
      // capture current streams so we can detect newly-created ones from the server
      const prevIds = Object.keys(streamsMap || {});

      const [streams, statuses, vdev, adev] = await Promise.all([
        apiGet('/streams'),
        apiGet('/status'),
        apiGet('/devices/video'),
        apiGet('/devices/audio')
      ]);

      streamsMap = {};
      (streams || []).forEach(s => { streamsMap[String(s.id)] = s; });

      statusMap = {};
      (statuses || []).forEach(st => { statusMap[String(st.id)] = st; });

      videoDevices = vdev || [];
      audioDevices = adev || [];

      renderAll();

      // if the server reported one or more new streams, and the user is not
      // currently viewing a stream (dashboard active), open the newly created
      // stream as if the user clicked it in the sidebar
      const newIds = Object.keys(streamsMap || {}).filter(id => !prevIds.includes(id));
      if (newIds.length > 0 && !activeStreamId) {
        // open the first new stream
        try { selectStream(newIds[0]); } catch (e) { console.debug('auto-select new stream failed', e); }
      }
      // update runtime output status indicators for the currently open stream without re-opening
      if (activeStreamId) {
        updateOutputRuntimeClasses(activeStreamId);
      }
    } catch (e) {
      console.error('refreshAll', e);
    }
  }

  function outputStatusToClass(s) {
    if (!s) return 'status-stopped';
    if (s === 'success') return 'status-live';
    if (s === 'fail') return 'status-error';
    // unknown -> treat as starting when output is enabled or stream progressing
    if (s === 'unknown') return 'status-starting';
    if (s === 'connecting' || s === 'pending') return 'status-starting';
    return 'status-stopped';
  }

  function updateOutputRuntimeClasses(streamId) {
    const st = statusMap[streamId];
    if (!st || !st.outputs) return;
    const stOutputs = st.outputs;

    $('#outputs-list .output-block').each(function() {
      const $block = $(this);
      const key = $block.attr('data-output-id');
      if (!key) return;

      // find matching runtime status by id
      let entry = stOutputs.find(x => String(x.id) === String(key));
      // fallback: try match by url if id isn't present
      if (!entry) {
        entry = stOutputs.find(x => String(x.id) === String(key));
      }

      const $toggle = $block.find('.output-toggle');
      if (!$toggle.length) return;

      let cls = outputStatusToClass(entry ? entry.status : null);
      // if user disabled the output (toggle not checked) show STOPPED color
      const isChecked = $toggle.hasClass('checked');
      if (!isChecked) cls = 'status-stopped';
      $toggle.removeClass('status-live status-starting status-error status-stopped').addClass(cls);
    });
  }

  function statusToBadge(st, stream){
    // Determine UI status using runtime status (st) and configured stream settings (stream)
    // If no runtime status, treat as STOPPED
    if (!st) return 'STOPPED';

    const outsStatus = st.outputs || [];

    // If any output reports success -> LIVE
    if (outsStatus.some(o => o.status === 'success')) return 'LIVE';

    // If stream config exists and has no enabled outputs -> STOPPED
    if (stream) {
      const cfgOuts = stream.outputs || [];
      const hasEnabled = cfgOuts.some(o => (o.enabled === undefined) ? true : !!o.enabled);
      if (!hasEnabled) return 'STOPPED';
    }

    // If any output failed -> ERROR
    if (outsStatus.some(o => o.status === 'fail')) return 'ERROR';

    // If sources are OK but outputs are not yet successful -> STARTING
    if (st.video_status === 'success' || st.audio_status === 'success') return 'STARTING';

    return 'STOPPED';
  }

  function renderSidebar(){
    const $list = $('#stream-list').empty();
    const $dash = $('<li/>').addClass('stream-item').attr('data-id','__dashboard__').append($('<div/>').text('\uD83D\uDCCA Dashboard'));
    $dash.on('click', ()=> showDashboard());
    $list.append($dash);

    Object.values(streamsMap).forEach(s => {
      const id = String(s.id);
      const st = statusMap[id];
      const status = statusToBadge(st, s);
      const $it = $('<li/>').addClass('stream-item').attr('data-id', id);
      if (id === activeStreamId) $it.addClass('active');
      const $dot = $('<span/>').addClass('dot');
      if (status === 'LIVE') $dot.addClass('status-live');
      else if (status === 'STARTING') $dot.addClass('status-starting');
      else if (status === 'ERROR') $dot.addClass('status-error');
      else $dot.addClass('status-stopped');

      const outputsCount = (s.outputs || []).length;
      const title = id;
      const meta = `${status} • ${outputsCount} outputs`;
      const $title = $('<div/>').append($('<div/>').text(title).addClass('stream-name')).append($('<div/>').text(meta).addClass('stream-meta'));
      $it.append($dot).append($title).on('click', ()=> showStream(id));
      $list.append($it);
    });
  }

  function renderDashboard(){
    const $tbody = $('#dashboard-table tbody').empty();
    let active = 0;

    Object.values(streamsMap).forEach(s => {
      const id = String(s.id);
      const st = statusMap[id] || {};
      const tr = $('<tr/>');
      const name = id;
      const status = statusToBadge(st, s);
      const uptime = '-';
      const resolution = (s.video && (s.video.width || s.video.height)) ? `${s.video.width||"-"}x${s.video.height||"-"}@${s.video.fps_n||0}` : '-';
      const bitrate = '-';
      const outputs = (s.outputs || []).length;
      const health = st.outputs ? Math.round(((st.outputs.filter(o=>o.status==='success').length) / Math.max(1, st.outputs.length)) * 100) : 0;

      tr.append($('<td/>').text(name));
      tr.append($('<td/>').text(status));
      tr.append($('<td/>').text(uptime));
      tr.append($('<td/>').text(resolution));
      tr.append($('<td/>').text(bitrate));
      tr.append($('<td/>').text(outputs));
      tr.append($('<td/>').append($('<span/>').addClass('health-meter').text(health + '%')));
      $tbody.append(tr);

      if (status === 'LIVE') active++;
    });

    $('#metric-active').text(active);
    $('#metric-bitrate').text('-');
    $('#metric-dropped').text('-');
    $('#metric-cpu').text('-');
    $('#metric-gpu').text('-');
    $('#metric-net').text('-');
  }

  async function renderStreamView(id){
    try {
      const s = await apiGet(`/streams/${id}`);
      activeStreamId = id;
      $('.view').addClass('hidden');
      $('#stream-view').removeClass('hidden');

      const st = statusMap[id] || {};
      const $sb = $('#status-bar').empty();
      const $dot = $('<span/>').addClass('dot');
      const uiStatus = statusToBadge(st, s);
      if (uiStatus==='LIVE') $dot.addClass('status-live'); else if (uiStatus==='STARTING') $dot.addClass('status-starting'); else if (uiStatus==='ERROR') $dot.addClass('status-error'); else $dot.addClass('status-stopped');
      $sb.append($dot).append($('<span/>').addClass('stream-name').text(`${uiStatus} ${id}`));
      $sb.append($('<span/>').addClass('status-item').text(`Resolution: ${s.video? `${s.video.width||""}x${s.video.height||""}`: '-'}`));
      $sb.append($('<span/>').addClass('status-item').text(`Outputs: ${(s.outputs||[]).length}`));

      // set video source status dot in the video card header
      const vclass = sourceStatusToClass(st.video_status);
      const $vdot = $('#video-dot');
      if ($vdot.length) {
        $vdot.removeClass('status-live status-starting status-error status-stopped').addClass(vclass);
      }
      // set audio source status dot in the audio card header
      const aclass = sourceStatusToClass(st.audio_status);
      const $adot = $('#audio-dot');
      if ($adot.length) {
        $adot.removeClass('status-live status-starting status-error status-stopped').addClass(aclass);
      }

      // video device select
      const $sources = $('#sources-list').empty();
      const curVideo = s.video && s.video.device ? s.video.device : '';
      const $sel = $('<select/>').css('width','100%');
      $sel.append($('<option/>').attr('value','').text('(none)'));
      videoDevices.forEach(d => { $sel.append($('<option/>').attr('value',d.id).text(d.name)); });
      $sel.val(curVideo);
      $sources.append($('<div/>').append($('<label/>').text('Video device')).append($sel));

      $sel.off('change').on('change', async ()=>{
        const newDevice = $sel.val();
        if (newDevice) s.video = Object.assign(s.video||{}, { device: newDevice }); else s.video = undefined;
        const payload = buildStreamPayload(s);
        await apiPut(`/streams/${id}`, payload);
        await refreshAll();
        await renderStreamView(id);
      });

      // populate video numeric inputs and codec/bitrate controls
      // values from s.video: width,height,fps_n,fps_d,codec,bitrate
      $('#video-width').val(s.video && s.video.width ? s.video.width : '');
      $('#video-height').val(s.video && s.video.height ? s.video.height : '');
      // fps: single text field accepts integer (30) or fraction (30000/1001)
      let fpsVal = '';
      if (s.video && s.video.fps_n) {
        if (s.video.fps_d && s.video.fps_d !== 1) fpsVal = `${s.video.fps_n}/${s.video.fps_d}`;
        else fpsVal = `${s.video.fps_n}`;
      }
      $('#video-fps').val(fpsVal);
      $('#video-codec').val(s.video && s.video.codec ? s.video.codec : 'avc');
      $('#video-bitrate').val(s.video && s.video.bitrate ? s.video.bitrate : '');

      // attach handlers: update stream when any video setting changes
      $('#video-width, #video-height, #video-fps, #video-codec, #video-bitrate').off('change').on('change', async function(){
        s.video = s.video || {};
        const w = parseInt($('#video-width').val()) || 0;
        const h = parseInt($('#video-height').val()) || 0;
        const fpsText = String($('#video-fps').val() || '').trim();
        const codec = $('#video-codec').val();
        const vb = parseInt($('#video-bitrate').val()) || 0;

        if (w > 0) s.video.width = w; else delete s.video.width;
        if (h > 0) s.video.height = h; else delete s.video.height;

        // parse fps: integer or fraction
        if (fpsText.indexOf('/') !== -1) {
          const parts = fpsText.split('/').map(p => parseInt(p.trim(), 10));
          if (parts.length === 2 && Number.isFinite(parts[0]) && Number.isFinite(parts[1]) && parts[0] > 0 && parts[1] > 0) {
            s.video.fps_n = parts[0];
            s.video.fps_d = parts[1];
          } else {
            delete s.video.fps_n;
            delete s.video.fps_d;
          }
        } else {
          const fn = parseInt(fpsText, 10);
          if (Number.isFinite(fn) && fn > 0) {
            s.video.fps_n = fn;
            s.video.fps_d = 1;
          } else {
            delete s.video.fps_n;
            delete s.video.fps_d;
          }
        }

        if (codec) s.video.codec = codec;
        if (vb > 0) s.video.bitrate = vb; else delete s.video.bitrate;

        const payload = buildStreamPayload(s);
        await apiPut(`/streams/${id}`, payload);
        await refreshAll();
        await renderStreamView(id);
      });

      // audio
      const $audio = $('#audio-list').empty();
      const curAudio = s.audio && s.audio.device ? s.audio.device : '';
      const $asel = $('<select/>').css('width','100%');
      $asel.append($('<option/>').attr('value','').text('(none)'));
      audioDevices.forEach(d => { $asel.append($('<option/>').attr('value',d.id).text(d.name)); });
      $asel.val(curAudio);
      $audio.append($('<div/>').append($('<label/>').text('Audio device')).append($asel));

      $asel.off('change').on('change', async ()=>{
        const newDevice = $asel.val();
        if (newDevice) s.audio = Object.assign(s.audio||{}, { device: newDevice }); else s.audio = undefined;
        const payload = buildStreamPayload(s);
        await apiPut(`/streams/${id}`, payload);
        await refreshAll();
        await renderStreamView(id);
      });

      // populate audio controls
      $('#audio-samplerate').val(s.audio && s.audio.sampleRate ? s.audio.sampleRate : '48000');
      $('#audio-channels').val(s.audio && s.audio.channels ? s.audio.channels : '2');
      $('#audio-codec').val(s.audio && s.audio.codec ? s.audio.codec : 'aac');
      $('#audio-bitrate').val(s.audio && s.audio.bitrate ? Math.round(s.audio.bitrate/1000) : '128');

      // audio change handlers
      $('#audio-samplerate, #audio-channels, #audio-codec, #audio-bitrate').off('change').on('change', async function(){
        s.audio = s.audio || {};
        const sr = parseInt($('#audio-samplerate').val()) || 48000;
        const ch = parseInt($('#audio-channels').val()) || 2;
        const codec = $('#audio-codec').val();
        const ab = parseInt($('#audio-bitrate').val()) || 128;

        s.audio.sampleRate = sr;
        s.audio.channels = ch;
        s.audio.codec = codec;
        // server expects bitrate in bits/sec
        s.audio.bitrate = ab * 1000;

        const payload = buildStreamPayload(s);
        await apiPut(`/streams/${id}`, payload);
        await refreshAll();
        await renderStreamView(id);
      });

      // outputs: render up to 5 outputs with status dot, number, toggle and fields
      const $outs = $('#outputs-list').empty();
      s.outputs = s.outputs || [];
      const stOutputs = (st && st.outputs) ? st.outputs : [];

      function findOutputStatus(o) {
        // try match by id then url
        if (!o) return null;
        const byId = stOutputs.find(x => x.id && o.id && String(x.id) === String(o.id));
        if (byId) return byId.status;
        const byUrl = stOutputs.find(x => x.id && o.url && String(x.url) === String(o.url));
        if (byUrl) return byUrl.status;
        return null;
      }

      s.outputs.forEach((o, idx) => {
        const num = idx + 1;
        const status = findOutputStatus(o);
        let statusClass;
        if (status === 'success') {
          // only show LIVE color when this output is enabled
          statusClass = o.enabled ? 'status-live' : 'status-stopped';
        } else if (status === 'fail') {
          // only show ERROR when enabled
          statusClass = o.enabled ? 'status-error' : 'status-stopped';
        } else {
          // unknown runtime status
          if (o.enabled) {
            // if the stream as a whole is starting (sources OK but outputs not yet successful), show STARTING
            const overall = statusToBadge(st, s);
            if (overall === 'STARTING') statusClass = 'status-starting';
            else statusClass = 'status-stopped';
          } else {
            statusClass = 'status-stopped';
          }
        }

        const $block = $('<div/>').addClass('output-block');
        // expose output id (or url fallback) for runtime matching
        if (o.id) $block.attr('data-output-id', o.id);
        else if (o.url) $block.attr('data-output-id', o.url);
        // do not dim or disable the block when toggled off; controls remain editable

        const $header = $('<div/>').addClass('output-header');
        const $info = $('<div/>').addClass('output-info');
        // move toggle into the info area and remove separate status dot
        const $toggle = $('<div/>').addClass('output-toggle').toggleClass('checked', !!o.enabled).attr('role','switch');
        // apply runtime status color class (respecting enabled state)
        $toggle.removeClass('status-live status-starting status-error status-stopped').addClass(statusClass);
        const $num = $('<span/>').addClass('output-number').text('#' + num);

        $toggle.on('click', async ()=>{
          o.enabled = !o.enabled;
          if (o.enabled) {
            $toggle.addClass('checked');
            // immediately show STARTING color when user enables output
            $toggle.removeClass('status-live status-error status-stopped').addClass('status-starting');
          } else {
            $toggle.removeClass('checked');
            // when user disables, show STOPPED color immediately
            $toggle.removeClass('status-live status-starting status-error').addClass('status-stopped');
          }
          const payload = buildStreamPayload(s);
          await apiPut(`/streams/${id}`, payload);
          await refreshAll();
          await renderStreamView(id);
        });

        $info.append($toggle).append($num);
        $header.append($info);
        $block.append($header);

        const $fields = $('<div/>').addClass('output-fields');
        const $type = $('<select/>').addClass('form-control').css('max-width','140px');
        $type.append($('<option/>').attr('value','rtmp').text('RTMP'));
        $type.append($('<option/>').attr('value','rtsp').text('RTSP'));
        $type.val(o.type || 'rtmp');
        const $url = $('<input/>').addClass('form-control').attr('type','text').val(o.url || '');

        $type.on('change', async ()=>{
          o.type = $type.val();
          const payload = buildStreamPayload(s);
          await apiPut(`/streams/${id}`, payload);
          await refreshAll();
        });

        let saveTimeout = null;
        $url.on('input', ()=>{
          clearTimeout(saveTimeout);
          saveTimeout = setTimeout(async ()=>{
            o.url = $url.val();
            const payload = buildStreamPayload(s);
            await apiPut(`/streams/${id}`, payload);
            await refreshAll();
          }, 600);
        });

        let $remove = null;
        // show remove button only when more than one output exists
        if ((s.outputs || []).length > 1) {
          $remove = $('<button/>').addClass('small danger').text('Remove').on('click', async ()=>{
            if (!confirm('Remove output #'+(idx+1)+'? This operation cannot be undone.')) return;
            s.outputs.splice(idx,1);
            const payload = buildStreamPayload(s);
            await apiPut(`/streams/${id}`, payload);
            await refreshAll();
            await renderStreamView(id);
          });
        }

        $fields.append($type).append($url);
        if ($remove) $fields.append($remove);
        $block.append($fields);
        $outs.append($block);
      });

      // add new output handler with max 5 outputs
      $('#btn-add-output').off('click').on('click', async ()=>{
        s.outputs = s.outputs || [];
        if (s.outputs.length >= 5) {
          alert('Maximum 5 outputs allowed');
          return;
        }
        // remember current count so we can detect server-side changes
        const prevCount = s.outputs.length;
        // default new output: create disabled so it doesn't start immediately
        s.outputs.push({ type: 'rtmp', url: 'rtmp://', enabled: false });
        const payload = buildStreamPayload(s);
        await apiPut(`/streams/${id}`, payload);
        await refreshAll();
        await renderStreamView(id);

        // Defensive check: if backend ignored the enabled flag and enabled the
        // newly-created output, explicitly disable it again.
        try {
          const updated = streamsMap[String(id)];
          if (updated && Array.isArray(updated.outputs) && updated.outputs.length > prevCount) {
            const newOut = updated.outputs[prevCount];
            if (newOut && newOut.enabled) {
              // flip locally then push to server to ensure disabled
              // find corresponding local output entry (by position)
              if (s.outputs[prevCount]) s.outputs[prevCount].enabled = false;
              await apiPut(`/streams/${id}`, buildStreamPayload(s));
              await refreshAll();
              await renderStreamView(id);
            }
          }
        } catch (e) {
          console.debug('post-add-output guard failed', e);
        }
      });

      // disable add button if already 5 outputs
      if (s.outputs.length >= 5) {
        $('#btn-add-output').prop('disabled', true).attr('title','Maximum 5 outputs');
      } else {
        $('#btn-add-output').prop('disabled', false).removeAttr('title');
      }

      $('#btn-start').off('click').on('click', async ()=>{
        s.outputs = s.outputs || [];
        s.outputs.forEach(o=> o.enabled = true);
        await apiPut(`/streams/${id}`, buildStreamPayload(s));
        await refreshAll();
      });

      $('#btn-stop').off('click').on('click', async ()=>{
        s.outputs = s.outputs || [];
        s.outputs.forEach(o=> o.enabled = false);
        await apiPut(`/streams/${id}`, buildStreamPayload(s));
        await refreshAll();
      });

      $('#btn-restart').off('click').on('click', async ()=>{
        s.outputs = s.outputs || [];
        s.outputs.forEach(o=> o.enabled = false);
        await apiPut(`/streams/${id}`, buildStreamPayload(s));
        s.outputs.forEach(o=> o.enabled = true);
        await apiPut(`/streams/${id}`, buildStreamPayload(s));
        await refreshAll();
      });

      $('#btn-delete-stream').off('click').on('click', async ()=>{
        if (!confirm('Delete stream '+id+'?')) return;
        await apiDelete(`/streams/${id}`);
        await refreshAll();
        showDashboard();
      });

    } catch (e) {
      console.error('renderStreamView', e);
    }
  }

  function buildStreamPayload(s){
    const payload = {};
    if (s.video) payload.video = s.video;
    if (s.audio) payload.audio = s.audio;
    if (s.outputs) payload.outputs = s.outputs.map(o => ({ type: o.type, url: o.url, enabled: !!o.enabled }));
    return payload;
  }

  function renderAll(){ renderSidebar(); renderDashboard(); }

  function showDashboard(){ activeStreamId = null; $('.view').addClass('hidden'); $('#dashboard-view').removeClass('hidden'); renderAll(); }
  function showStream(id){ activeStreamId = id; $('.view').addClass('hidden'); $('#stream-view').removeClass('hidden'); renderStreamView(id); }
  // ensure sidebar reflects active selection when navigating programmatically
  function selectStream(id){
    activeStreamId = id;
    renderSidebar();
    $('.view').addClass('hidden');
    $('#stream-view').removeClass('hidden');
    renderStreamView(id);
  }

  // create stream: create with one disabled default output without prompts
  $('#btn-new-stream').on('click', async ()=>{
    try{
      const body = { outputs: [ { type: 'rtmp', url: 'rtmp://', enabled: false } ] };
      const resp = await apiPost('/streams', body);
      if (resp.status === 201) {
        const data = await resp.json();
        await refreshAll();
        // open newly created stream for editing
        if (data && data.id) selectStream(String(data.id));
      } else {
        const txt = await resp.text(); alert('Create failed: '+resp.status+' '+txt);
      }
    } catch (e) { console.error('create stream', e); alert('Create failed'); }
  });



  // initial load and polling
  refreshAll();
  setInterval(refreshAll, POLL_MS);

});
