import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import 'package:http_parser/http_parser.dart';
import 'package:network_info_plus/network_info_plus.dart';
import 'package:record/record.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  runApp(const AirRobotApp());
}

class AirRobotApp extends StatelessWidget {
  const AirRobotApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'AIRobot',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(
          seedColor: const Color(0xFF146C94),
          brightness: Brightness.light,
        ),
        useMaterial3: true,
      ),
      home: const AirRobotHomePage(),
    );
  }
}

class RobotStatus {
  const RobotStatus({
    required this.online,
    required this.baseUrl,
    required this.voiceStage,
    required this.userName,
    required this.assistantName,
    required this.wifiConnected,
    required this.ipAddress,
    required this.accompanimentEnabled,
    required this.looping,
    required this.transcript,
    required this.reply,
    required this.volume,
  });

  final bool online;
  final String baseUrl;
  final String voiceStage;
  final String userName;
  final String assistantName;
  final bool wifiConnected;
  final String ipAddress;
  final bool accompanimentEnabled;
  final bool looping;
  final String transcript;
  final String reply;
  final int volume;

  factory RobotStatus.fromJson(Map<String, dynamic> json, String baseUrl) {
    int parsedVolume = 50;
    final dynamic volumeValue = json['volume'];
    if (volumeValue is int) {
      parsedVolume = volumeValue;
    } else if (volumeValue is String) {
      parsedVolume = int.tryParse(volumeValue) ?? 50;
    }

    return RobotStatus(
      online: true,
      baseUrl: baseUrl,
      voiceStage: (json['voice_stage'] ?? 'idle').toString(),
      userName: (json['user_name'] ?? '用户').toString(),
      assistantName: (json['assistant_name'] ?? json['robot_name'] ?? 'AIRobot').toString(),
      wifiConnected: json['wifi_connected'] == true,
      ipAddress: (json['ip'] ?? '').toString(),
      accompanimentEnabled: json['accompaniment'] == true || json['musicbox_accompaniment'] == true,
      looping: json['looping'] == true || json['melody_playing'] == true,
      transcript: (json['last_transcript'] ?? '').toString(),
      reply: (json['last_reply'] ?? '').toString(),
      volume: parsedVolume.clamp(0, 100),
    );
  }
}

class AirRobotHomePage extends StatefulWidget {
  const AirRobotHomePage({super.key});

  @override
  State<AirRobotHomePage> createState() => _AirRobotHomePageState();
}

class _AirRobotHomePageState extends State<AirRobotHomePage> {
  static const _prefsBaseUrlKey = 'base_url';
  static const _prefsLastGoodUrlKey = 'last_good_url';
  static const _prefsSongKey = 'song';
  static const _prefsInstrumentKey = 'instrument';
  final _baseUrlController = TextEditingController(text: 'http://airobot.local');
  final _askController = TextEditingController();
  final _sayController = TextEditingController();
  final _commandController = TextEditingController();
  final AudioRecorder _recorder = AudioRecorder();
  final List<String> _songs = const ['canon', 'castle_in_the_sky', 'jingle_bells'];
  final List<String> _instruments = const ['musicbox', 'piano'];

  Timer? _pollTimer;
  RobotStatus? _robotStatus;
  String _statusText = '正在准备连接';
  String _voiceHint = '长按按钮录音，松开发送';
  String _transcript = '';
  String _reply = '';
  String _selectedSong = 'canon';
  String _selectedInstrument = 'musicbox';
  String _lastGoodUrl = '';
  bool _busy = false;
  bool _discovering = false;
  bool _recording = false;
  bool _pressed = false;
  double _volume = 50;
  Timer? _recordingTimeoutTimer;

  @override
  void initState() {
    super.initState();
    _bootstrap();
  }

  @override
  void dispose() {
    _pollTimer?.cancel();
    _baseUrlController.dispose();
    _askController.dispose();
    _sayController.dispose();
    _commandController.dispose();
    _recordingTimeoutTimer?.cancel();
    _recorder.dispose();
    super.dispose();
  }

  Future<void> _bootstrap() async {
    await _restorePreferences();
    await _discoverAndRefresh(showProgress: false);
    _pollTimer = Timer.periodic(
      const Duration(seconds: 3),
      (_) => _refreshStatus(silent: true),
    );
  }

  Future<void> _restorePreferences() async {
    final prefs = await SharedPreferences.getInstance();
    final savedBaseUrl = prefs.getString(_prefsBaseUrlKey);
    final savedLastGoodUrl = prefs.getString(_prefsLastGoodUrlKey);
    final savedSong = prefs.getString(_prefsSongKey);
    final savedInstrument = prefs.getString(_prefsInstrumentKey);
    if (savedBaseUrl != null && savedBaseUrl.isNotEmpty) {
      _baseUrlController.text = _normalizedUrl(savedBaseUrl);
    }
    if (savedLastGoodUrl != null) {
      _lastGoodUrl = _normalizedUrl(savedLastGoodUrl);
    }
    if (savedSong != null && _songs.contains(savedSong)) {
      _selectedSong = savedSong;
    }
    if (savedInstrument != null && _instruments.contains(savedInstrument)) {
      _selectedInstrument = savedInstrument;
    }
  }

  Future<void> _savePreferences() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_prefsBaseUrlKey, _normalizedUrl(_baseUrlController.text));
    await prefs.setString(_prefsLastGoodUrlKey, _lastGoodUrl);
    await prefs.setString(_prefsSongKey, _selectedSong);
    await prefs.setString(_prefsInstrumentKey, _selectedInstrument);
  }

  String _normalizedUrl(String value) {
    final trimmed = value.trim();
    if (trimmed.isEmpty) {
      return 'http://airobot.local';
    }
    final withScheme = trimmed.startsWith('http://') || trimmed.startsWith('https://')
        ? trimmed
        : 'http://$trimmed';
    final uri = Uri.tryParse(withScheme);
    if (uri == null || uri.host.isEmpty) {
      return withScheme;
    }

    String host = uri.host.toLowerCase();
    if (host == 'ariobot.local' || host == 'airbot.local' || host == 'arobot.local') {
      host = 'airobot.local';
    }

    if (host == 'www.airobot.local') {
      host = 'airobot.local';
    }

    final normalized = uri.replace(host: host);
    return normalized.toString();
  }

  Uri _uri(String baseUrl, String path) {
    final root = _normalizedUrl(baseUrl);
    return Uri.parse('$root$path');
  }

  String _voiceStageText(String stage) {
    switch (stage) {
      case 'uploading':
        return '上传中';
      case 'recognizing':
        return '识别中';
      case 'thinking':
        return '思考中';
      case 'speaking':
        return '播放中';
      default:
        return '空闲';
    }
  }

  String _formatHttpError(Object error) {
    if (error is SocketException) {
      return '无法连接设备，请确认手机和 AIRobot 在同一 Wi‑Fi。';
    }
    if (error is TimeoutException) {
      return '设备响应超时。';
    }
    return '请求失败：$error';
  }

  String _sanitizeJsonBody(String body) {
    final buffer = StringBuffer();
    for (final codeUnit in body.codeUnits) {
      final isPrintable = codeUnit >= 32;
      final isAllowedWhitespace = codeUnit == 9 || codeUnit == 10 || codeUnit == 13;
      if (isPrintable || isAllowedWhitespace) {
        buffer.writeCharCode(codeUnit);
      }
    }
    return buffer.toString();
  }

  Future<RobotStatus?> _tryFetchStatus(String baseUrl) async {
    try {
      debugPrint('AIRobot try status: $baseUrl');
      final response = await http
          .get(_uri(baseUrl, '/api/status'))
          .timeout(const Duration(seconds: 2));
      if (response.statusCode != 200) {
        debugPrint('AIRobot status non-200: $baseUrl -> ${response.statusCode}');
        return null;
      }
      final sanitizedBody = _sanitizeJsonBody(response.body);
      final data = jsonDecode(sanitizedBody);
      if (data is! Map<String, dynamic>) {
        return null;
      }
      debugPrint('AIRobot status ok: $baseUrl');
      return RobotStatus.fromJson(data, _normalizedUrl(baseUrl));
    } catch (error) {
      debugPrint('AIRobot status error: $baseUrl -> $error');
      return null;
    }
  }

  Future<List<String>> _candidateBaseUrls() async {
    final urls = <String>{};
    urls.add(_normalizedUrl(_baseUrlController.text));
    if (_lastGoodUrl.isNotEmpty) {
      urls.add(_normalizedUrl(_lastGoodUrl));
    }
    urls.add('http://airobot.local');
    urls.add('http://192.168.4.1');
    urls.add('http://192.168.1.7');

    final wifiIp = await NetworkInfo().getWifiIP();
    if (wifiIp != null && wifiIp.isNotEmpty) {
      final parts = wifiIp.split('.');
      if (parts.length == 4) {
        final prefix = '${parts[0]}.${parts[1]}.${parts[2]}';
        for (final host in <int>[1, 7, 23, 50, 100, 150, 200]) {
          urls.add('http://$prefix.$host');
        }
      }
    }
    return urls.toList();
  }

  Future<String?> _scanSubnetForRobot() async {
    final prefixes = <String>{'192.168.1', '192.168.0', '192.168.31'};
    final wifiIp = await NetworkInfo().getWifiIP();
    if (wifiIp != null && wifiIp.isNotEmpty) {
      final parts = wifiIp.split('.');
      if (parts.length == 4) {
        prefixes.add('${parts[0]}.${parts[1]}.${parts[2]}');
      }
    }

    for (final prefix in prefixes) {
      for (int start = 1; start <= 254; start += 16) {
        final futures = <Future<RobotStatus?>>[];
        for (int host = start; host < start + 16 && host <= 254; host++) {
          futures.add(_tryFetchStatus('http://$prefix.$host'));
        }
        final results = await Future.wait(futures);
        for (final status in results) {
          if (status != null) {
            return status.baseUrl;
          }
        }
      }
    }
    return null;
  }

  void _applyStatus(RobotStatus status) {
    setState(() {
      _robotStatus = status;
      _statusText = '已连接到 ${status.baseUrl}';
      _transcript = status.transcript;
      _reply = status.reply;
      _volume = status.volume.toDouble();
    });
  }

  Future<void> _discoverAndRefresh({required bool showProgress}) async {
    if (_discovering) {
      return;
    }
    setState(() {
      _discovering = true;
      if (showProgress) {
        _statusText = '正在查找 AIRobot...';
      }
    });

    final candidates = await _candidateBaseUrls();
    debugPrint('AIRobot candidates: ${candidates.join(", ")}');
    for (final url in candidates) {
      final status = await _tryFetchStatus(url);
      if (status != null) {
        debugPrint('AIRobot discovered via candidate: ${status.baseUrl}');
        _lastGoodUrl = status.baseUrl;
        _baseUrlController.text = status.baseUrl;
        await _savePreferences();
        _applyStatus(status);
        setState(() => _discovering = false);
        return;
      }
    }

    final scannedUrl = await _scanSubnetForRobot();
    if (scannedUrl != null) {
      debugPrint('AIRobot discovered via subnet scan: $scannedUrl');
      final status = await _tryFetchStatus(scannedUrl);
      if (status != null) {
        _lastGoodUrl = status.baseUrl;
        _baseUrlController.text = status.baseUrl;
        await _savePreferences();
        _applyStatus(status);
        setState(() => _discovering = false);
        return;
      }
    }

    setState(() {
      _discovering = false;
      _statusText = '没有找到 AIRobot，请检查 Wi‑Fi 或手动填写地址。';
    });
    debugPrint('AIRobot discovery failed');
  }

  Future<void> _refreshStatus({bool silent = false}) async {
    final status = await _tryFetchStatus(_baseUrlController.text);
    if (status != null) {
      _lastGoodUrl = status.baseUrl;
      await _savePreferences();
      _applyStatus(status);
      return;
    }
    if (!silent) {
      setState(() {
        _statusText = '当前地址无法连接，正在尝试自动发现。';
      });
    }
    await _discoverAndRefresh(showProgress: !silent);
  }

  Future<void> _postForm(
    String path,
    Map<String, String> body, {
    String successText = '已发送',
  }) async {
    setState(() => _busy = true);
    try {
      final response = await http
          .post(_uri(_baseUrlController.text, path), body: body)
          .timeout(const Duration(seconds: 12));
      if (response.statusCode >= 200 && response.statusCode < 300) {
        setState(() => _statusText = successText);
        await _refreshStatus(silent: true);
      } else {
        setState(() => _statusText = '请求失败：${response.statusCode}');
      }
    } catch (error) {
      setState(() => _statusText = _formatHttpError(error));
      await _discoverAndRefresh(showProgress: false);
    } finally {
      if (mounted) {
        setState(() => _busy = false);
      }
    }
  }

  Future<void> _sendVoiceFile(String path) async {
    setState(() {
      _busy = true;
      _voiceHint = '正在上传语音...';
      _statusText = '语音已录制，准备发送。';
    });
    try {
      final request = http.MultipartRequest(
        'POST',
        _uri(_baseUrlController.text, '/api/asr-chat'),
      );
      request.files.add(await http.MultipartFile.fromPath(
        'audio',
        path,
        contentType: MediaType('audio', 'aac'),
      ));
      final response = await request.send().timeout(const Duration(seconds: 25));
      final body = await response.stream.bytesToString();
      debugPrint('AIRobot voice response: ${response.statusCode} $body');
      if (response.statusCode >= 200 && response.statusCode < 300) {
        final data = body.isNotEmpty ? jsonDecode(body) : <String, dynamic>{};
        setState(() {
          _transcript = (data['transcript'] ?? _transcript).toString();
          _reply = (data['reply'] ?? _reply).toString();
          _voiceHint = '长按按钮录音，松开发送';
          _statusText = '语音发送完成。';
        });
        await _refreshStatus(silent: true);
      } else {
        setState(() {
          _voiceHint = '发送失败，请重试';
          _statusText = body.isNotEmpty
              ? '语音失败：$body'
              : '语音请求失败：${response.statusCode}';
        });
      }
    } catch (error) {
      debugPrint('AIRobot voice exception: $error');
      setState(() {
        _voiceHint = '发送失败，请检查连接';
        _statusText = _formatHttpError(error);
      });
      await _discoverAndRefresh(showProgress: false);
    } finally {
      final file = File(path);
      if (await file.exists()) {
        await file.delete();
      }
      if (mounted) {
        setState(() {
          _busy = false;
          _recording = false;
          _pressed = false;
        });
      }
    }
  }

  Future<void> _startRecording() async {
    if (_busy || _recording) {
      return;
    }
    final granted = await _recorder.hasPermission();
    if (!granted) {
      setState(() {
        _voiceHint = '没有麦克风权限，请在系统设置中允许录音。';
        _statusText = '录音权限被拒绝。';
      });
      return;
    }
    final dir = Directory.systemTemp;
    final path = '${dir.path}\\airobot_${DateTime.now().millisecondsSinceEpoch}.aac';
    await _recorder.start(
      const RecordConfig(
        encoder: AudioEncoder.aacLc,
        numChannels: 1,
        bitRate: 24000,
        sampleRate: 32000,
      ),
      path: path,
    );
    _recordingTimeoutTimer?.cancel();
    _recordingTimeoutTimer = Timer(const Duration(seconds: 5), () {
      if (_recording) {
        _stopRecordingAndSend();
      }
    });
    setState(() {
      _recording = true;
      _pressed = true;
      _voiceHint = '正在录音，最长 5 秒，松开发送';
      _statusText = '录音中...';
    });
  }

  Future<void> _stopRecordingAndSend() async {
    if (!_recording) {
      return;
    }
    _recordingTimeoutTimer?.cancel();
    final path = await _recorder.stop();
    if (path == null || path.isEmpty) {
      setState(() {
        _recording = false;
        _pressed = false;
        _voiceHint = '录音失败，请重试';
      });
      return;
    }
    await _sendVoiceFile(path);
  }

  Future<void> _cancelRecording() async {
    if (_recording) {
      _recordingTimeoutTimer?.cancel();
      await _recorder.stop();
    }
    setState(() {
      _recording = false;
      _pressed = false;
      _voiceHint = '录音已取消';
      _statusText = '未发送语音。';
    });
  }

  Widget _card(String title, Widget child, {Widget? action}) {
    return Card(
      elevation: 0,
      margin: const EdgeInsets.only(bottom: 16),
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(24)),
      child: Padding(
        padding: const EdgeInsets.all(18),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Expanded(
                  child: Text(
                    title,
                    style: const TextStyle(fontSize: 18, fontWeight: FontWeight.w700),
                  ),
                ),
                if (action != null) action,
              ],
            ),
            const SizedBox(height: 14),
            child,
          ],
        ),
      ),
    );
  }

  Widget _statusTile(String label, String value) {
    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: const Color(0xFFF4F8FB),
        borderRadius: BorderRadius.circular(18),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(label, style: const TextStyle(fontSize: 12, color: Colors.black54)),
          const SizedBox(height: 6),
          Text(value, style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w600)),
        ],
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final status = _robotStatus;
    final online = status != null && status.online;

    return Scaffold(
      backgroundColor: const Color(0xFFE9F2F7),
      appBar: AppBar(
        title: const Text('AIRobot 控制台'),
        centerTitle: false,
      ),
      body: SafeArea(
        child: RefreshIndicator(
          onRefresh: () => _refreshStatus(),
          child: ListView(
            padding: const EdgeInsets.all(16),
            children: [
              Container(
                padding: const EdgeInsets.all(20),
                decoration: BoxDecoration(
                  gradient: const LinearGradient(
                    colors: [Color(0xFF146C94), Color(0xFF19A7CE)],
                    begin: Alignment.topLeft,
                    end: Alignment.bottomRight,
                  ),
                  borderRadius: BorderRadius.circular(28),
                ),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      online ? '机器人已在线' : '等待连接 AIRobot',
                      style: const TextStyle(
                        color: Colors.white,
                        fontSize: 26,
                        fontWeight: FontWeight.w800,
                      ),
                    ),
                    const SizedBox(height: 10),
                    Text(
                      _statusText,
                      style: const TextStyle(color: Colors.white, fontSize: 15),
                    ),
                    const SizedBox(height: 16),
                    TextField(
                      controller: _baseUrlController,
                      decoration: InputDecoration(
                        filled: true,
                        fillColor: Colors.white,
                        hintText: '输入地址，例如 http://airobot.local',
                        border: OutlineInputBorder(
                          borderRadius: BorderRadius.circular(18),
                          borderSide: BorderSide.none,
                        ),
                      ),
                    ),
                    const SizedBox(height: 12),
                    Row(
                      children: [
                        Expanded(
                          child: FilledButton(
                            onPressed: _discovering ? null : () => _discoverAndRefresh(showProgress: true),
                            child: Text(_discovering ? '查找中...' : '自动发现'),
                          ),
                        ),
                        const SizedBox(width: 10),
                        Expanded(
                          child: OutlinedButton(
                            onPressed: _busy ? null : _refreshStatus,
                            style: OutlinedButton.styleFrom(
                              foregroundColor: Colors.white,
                              side: const BorderSide(color: Colors.white70),
                            ),
                            child: const Text('刷新状态'),
                          ),
                        ),
                      ],
                    ),
                  ],
                ),
              ),
              const SizedBox(height: 16),
              _card(
                '设备状态',
                GridView.count(
                  crossAxisCount: 2,
                  shrinkWrap: true,
                  physics: const NeverScrollableScrollPhysics(),
                  mainAxisSpacing: 10,
                  crossAxisSpacing: 10,
                  childAspectRatio: 1.5,
                  children: [
                    _statusTile('连接', online ? '正常' : '离线'),
                    _statusTile('语音阶段', _voiceStageText(status?.voiceStage ?? 'idle')),
                    _statusTile('用户', status?.userName ?? '未知'),
                    _statusTile('机器人', status?.assistantName ?? 'AIRobot'),
                    _statusTile('Wi‑Fi', status?.wifiConnected == true ? '已连接' : '未连接'),
                    _statusTile('IP', status?.ipAddress.isNotEmpty == true ? status!.ipAddress : '未知'),
                    _statusTile('伴奏', status?.accompanimentEnabled == true ? '开启' : '关闭'),
                    _statusTile('循环', status?.looping == true ? '开启' : '关闭'),
                  ],
                ),
              ),
              _card(
                '语音对话',
                Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(_voiceHint, style: const TextStyle(fontSize: 15)),
                    const SizedBox(height: 14),
                    GestureDetector(
                      onLongPressStart: (_) => _startRecording(),
                      onLongPressEnd: (_) => _stopRecordingAndSend(),
                      onLongPressCancel: _cancelRecording,
                      child: AnimatedContainer(
                        duration: const Duration(milliseconds: 180),
                        height: 96,
                        decoration: BoxDecoration(
                          color: _pressed ? const Color(0xFF19A7CE) : const Color(0xFF146C94),
                          borderRadius: BorderRadius.circular(28),
                        ),
                        child: Center(
                          child: Text(
                            _recording ? '松开发送' : '按住说话',
                            style: const TextStyle(
                              color: Colors.white,
                              fontSize: 22,
                              fontWeight: FontWeight.w700,
                            ),
                          ),
                        ),
                      ),
                    ),
                    const SizedBox(height: 14),
                    if (_transcript.isNotEmpty) Text('识别结果：$_transcript'),
                    if (_reply.isNotEmpty) ...[
                      const SizedBox(height: 8),
                      Text('机器人回复：$_reply'),
                    ],
                  ],
                ),
              ),
              _card(
                '文本控制',
                Column(
                  children: [
                    TextField(
                      controller: _askController,
                      decoration: const InputDecoration(labelText: '聊天内容'),
                    ),
                    const SizedBox(height: 10),
                    FilledButton(
                      onPressed: _busy
                          ? null
                          : () => _postForm('/api/chat', {'text': _askController.text}, successText: '已发送聊天请求'),
                      child: const Text('发送聊天'),
                    ),
                    const SizedBox(height: 14),
                    TextField(
                      controller: _sayController,
                      decoration: const InputDecoration(labelText: '播报内容'),
                    ),
                    const SizedBox(height: 10),
                    FilledButton(
                      onPressed: _busy
                          ? null
                          : () => _postForm('/api/tts', {'text': _sayController.text}, successText: '已发送播报请求'),
                      child: const Text('让机器人说话'),
                    ),
                    const SizedBox(height: 14),
                    TextField(
                      controller: _commandController,
                      decoration: const InputDecoration(labelText: '原始命令'),
                    ),
                    const SizedBox(height: 10),
                    FilledButton(
                      onPressed: _busy
                          ? null
                          : () => _postForm('/api/command', {'line': _commandController.text}, successText: '命令已发送'),
                      child: const Text('发送命令'),
                    ),
                  ],
                ),
              ),
              _card(
                '音乐与音量',
                Column(
                  children: [
                    DropdownButtonFormField<String>(
                      initialValue: _selectedSong,
                      decoration: const InputDecoration(labelText: '歌曲'),
                      items: _songs
                          .map((song) => DropdownMenuItem(value: song, child: Text(song)))
                          .toList(),
                      onChanged: (value) {
                        if (value == null) return;
                        setState(() => _selectedSong = value);
                        _savePreferences();
                      },
                    ),
                    const SizedBox(height: 10),
                    DropdownButtonFormField<String>(
                      initialValue: _selectedInstrument,
                      decoration: const InputDecoration(labelText: '乐器'),
                      items: _instruments
                          .map((instrument) => DropdownMenuItem(value: instrument, child: Text(instrument)))
                          .toList(),
                      onChanged: (value) {
                        if (value == null) return;
                        setState(() => _selectedInstrument = value);
                        _savePreferences();
                      },
                    ),
                    const SizedBox(height: 10),
                    Row(
                      children: [
                        Expanded(
                          child: FilledButton(
                            onPressed: _busy
                                ? null
                                : () => _postForm('/api/melody/play', {
                                      'song': _selectedSong,
                                      'instrument': _selectedInstrument,
                                    }, successText: '已发送播放请求'),
                            child: const Text('播放音乐'),
                          ),
                        ),
                        const SizedBox(width: 10),
                        Expanded(
                          child: OutlinedButton(
                            onPressed: _busy
                                ? null
                                : () => _postForm('/api/melody/stop', {}, successText: '已发送停止请求'),
                            child: const Text('停止'),
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 12),
                    SwitchListTile(
                      value: status?.accompanimentEnabled ?? false,
                      title: const Text('伴奏'),
                      contentPadding: EdgeInsets.zero,
                      onChanged: _busy
                          ? null
                          : (value) => _postForm('/api/accompaniment', {
                                'enabled': value ? 'on' : 'off',
                              }, successText: value ? '已开启伴奏' : '已关闭伴奏'),
                    ),
                    const SizedBox(height: 8),
                    Row(
                      children: [
                        const Text('音量'),
                        Expanded(
                          child: Slider(
                            value: _volume,
                            min: 0,
                            max: 100,
                            divisions: 20,
                            label: _volume.round().toString(),
                            onChanged: (value) => setState(() => _volume = value),
                            onChangeEnd: (value) => _postForm('/api/volume', {
                              'value': value.round().toString(),
                            }, successText: '音量已更新'),
                          ),
                        ),
                        Text(_volume.round().toString()),
                      ],
                    ),
                  ],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
