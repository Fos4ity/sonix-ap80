// SonixSend.exe for MusicBee "Odeslat do -> Sonix" (external application, "<URL>").
// MusicBee starts one copy per track. Every copy drops its file into a shared
// queue and exits; the first one becomes the only window and uploads the whole
// queue over Sonix Wi-Fi transfer into /data/mnt/sd_0/Music/<Artist>/<Album>/
// (the two folders above the file on the PC).
// Address: SonixSend.txt next to the exe (one line), default http://192.168.1.100/.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Net;
using System.Text;
using System.Threading;
using System.Windows.Forms;

static class SonixSend
{
    const string CardMusic = "/data/mnt/sd_0/Music/";
    static readonly string QDir = Path.Combine(Path.GetTempPath(), "SonixSend");
    static readonly string QFile = Path.Combine(QDir, "queue.txt");
    static Mutex qLock = new Mutex(false, "Local\\SonixSendQueue");

    static readonly Color Bg = Color.FromArgb(30, 30, 30), Surface = Color.FromArgb(44, 44, 46),
        Text1 = Color.FromArgb(240, 240, 240), Text2 = Color.FromArgb(160, 160, 165), Accent = Color.FromArgb(0xd5, 0x61, 0x99);

    static Form form; static Label title, sub, count; static Bar total, file; static Button btn;
    static volatile bool cancel = false;
    static int done = 0, failed = 0, seen = 0;
    static List<string> errors = new List<string>();

    [STAThread]
    static void Main(string[] args)
    {
        Directory.CreateDirectory(QDir);
        List<string> mine = new List<string>();
        foreach (string a in args) Collect(a.Trim('"'), mine);
        Enqueue(mine);

        bool first;
        Mutex primary = new Mutex(true, "Local\\SonixSendPrimary", out first);
        if (!first) return; // the window that is already open takes it

        Application.EnableVisualStyles();
        BuildUi();
        string baseUrl = BaseUrl();
        form.Shown += delegate { Thread t = new Thread(delegate () { Worker(baseUrl); }); t.IsBackground = true; t.Start(); };
        Application.Run(form);
        primary.ReleaseMutex();
    }

    // ---------- queue ----------
    static void Enqueue(List<string> files)
    {
        if (files.Count == 0) return;
        qLock.WaitOne();
        try { File.AppendAllLines(QFile, files, Encoding.UTF8); } finally { qLock.ReleaseMutex(); }
    }

    static List<string> TakeAll()
    {
        qLock.WaitOne();
        try
        {
            if (!File.Exists(QFile)) return new List<string>();
            List<string> l = new List<string>();
            foreach (string s in File.ReadAllLines(QFile, Encoding.UTF8)) if (s.Trim().Length > 0) l.Add(s.Trim());
            File.Delete(QFile);
            return l;
        }
        finally { qLock.ReleaseMutex(); }
    }

    static void Collect(string a, List<string> files)
    {
        if (Directory.Exists(a)) { foreach (string f in Directory.GetFiles(a, "*", SearchOption.AllDirectories)) files.Add(f); return; }
        if (!File.Exists(a)) return;
        string ext = Path.GetExtension(a).ToLowerInvariant();
        if (ext == ".m3u" || ext == ".m3u8")
        {
            foreach (string line in File.ReadAllLines(a, Encoding.UTF8))
            {
                string l = line.Trim();
                if (l.Length == 0 || l.StartsWith("#")) continue;
                if (!Path.IsPathRooted(l)) l = Path.Combine(Path.GetDirectoryName(a), l);
                if (File.Exists(l)) files.Add(l);
            }
            return;
        }
        files.Add(a);
    }

    static string BaseUrl()
    {
        try
        {
            string f = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "SonixSend.txt");
            if (File.Exists(f)) { string s = File.ReadAllText(f).Trim(); if (s.Length > 0) { if (!s.StartsWith("http")) s = "http://" + s; return s.EndsWith("/") ? s : s + "/"; } }
        }
        catch { }
        return "http://192.168.1.100/";
    }

    // ---------- UI ----------
    class Bar : Control
    {
        public double Value;
        public Bar() { DoubleBuffered = true; Height = 6; }
        protected override void OnPaint(PaintEventArgs e)
        {
            e.Graphics.Clear(Surface);
            int w = (int)(Width * Math.Max(0, Math.Min(1, Value)));
            using (SolidBrush b = new SolidBrush(Accent)) e.Graphics.FillRectangle(b, 0, 0, w, Height);
        }
    }

    static Label MakeLabel(float size, bool bold, Color c, int top)
    {
        Label l = new Label();
        l.AutoSize = false; l.Left = 20; l.Top = top; l.Width = 440; l.Height = (int)(size * 2.1);
        l.Font = new Font("Segoe UI", size, bold ? FontStyle.Bold : FontStyle.Regular);
        l.ForeColor = c; l.BackColor = Bg; l.AutoEllipsis = true;
        return l;
    }

    static void BuildUi()
    {
        form = new Form();
        form.Text = "Sonix"; form.ClientSize = new Size(480, 200); form.BackColor = Bg;
        form.FormBorderStyle = FormBorderStyle.FixedSingle; form.MaximizeBox = false; form.MinimizeBox = true;
        form.StartPosition = FormStartPosition.CenterScreen; form.TopMost = true; form.ShowInTaskbar = true;
        try { form.Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch { }

        Label head = MakeLabel(10, false, Accent, 14); head.Text = "ODESÍLÁNÍ DO PŘEHRÁVAČE";
        title = MakeLabel(13, true, Text1, 38); title.Text = "Připojuji se…";
        sub = MakeLabel(9.5f, false, Text2, 68); sub.Text = "";
        file = new Bar(); file.Left = 20; file.Top = 98; file.Width = 440; file.Height = 4;
        total = new Bar(); total.Left = 20; total.Top = 112; total.Width = 440; total.Height = 8;
        count = MakeLabel(9.5f, false, Text2, 130); count.Width = 300; count.Text = "";

        btn = new Button(); btn.Text = "Zrušit"; btn.Left = 360; btn.Top = 150; btn.Width = 100; btn.Height = 32;
        btn.FlatStyle = FlatStyle.Flat; btn.FlatAppearance.BorderSize = 0; btn.BackColor = Surface; btn.ForeColor = Text1;
        btn.Font = new Font("Segoe UI", 9.5f);
        btn.Click += delegate { if (btn.Text == "Zavřít") form.Close(); else { cancel = true; btn.Enabled = false; btn.Text = "Ruším…"; } };

        form.Controls.AddRange(new Control[] { head, title, sub, file, total, count, btn });
    }

    static void Ui(Action a) { try { if (!form.IsDisposed) form.BeginInvoke(a); } catch { } }

    // ---------- work ----------
    static void Worker(string baseUrl)
    {
        HashSet<string> made = new HashSet<string>();
        Queue<string> todo = new Queue<string>();
        int idle = 0;
        while (!cancel)
        {
            foreach (string f in TakeAll()) { todo.Enqueue(f); seen++; }
            if (todo.Count == 0)
            {
                // MusicBee starts the copies one after another: wait a moment for stragglers.
                if (++idle > 15) break;
                Thread.Sleep(200);
                continue;
            }
            idle = 0;
            string path = todo.Dequeue();
            Send(baseUrl, path, made);
            if (failed > 0 && done == 0 && errors.Count > 0 && errors[0].StartsWith("!")) break; // player unreachable
        }
        if (cancel) { foreach (string f in TakeAll()) seen++; }
        Finish();
    }

    static void Send(string baseUrl, string path, HashSet<string> made)
    {
        string name = Path.GetFileName(path);
        DirectoryInfo album = new FileInfo(path).Directory;
        DirectoryInfo artist = album != null ? album.Parent : null;
        string artistName = Clean(artist != null ? artist.Name : null, "Unknown Artist");
        string albumName = Clean(album != null ? album.Name : null, "Unknown Album");
        string dir = CardMusic + artistName + "/" + albumName + "/";
        int n = done + failed + 1, of = seen;
        Ui(delegate {
            title.Text = Path.GetFileNameWithoutExtension(name);
            sub.Text = artistName + "  ·  " + albumName;
            count.Text = n + " z " + of;
            file.Value = 0; file.Invalidate();
            total.Value = (double)(n - 1) / Math.Max(1, of); total.Invalidate();
        });
        try
        {
            string here = CardMusic; EnsureDir(baseUrl, here, made);
            foreach (string p in new string[] { artistName, albumName }) { here += p + "/"; EnsureDir(baseUrl, here, made); }
            PostFile(baseUrl, dir, path);
            done++;
        }
        catch (Exception ex)
        {
            failed++;
            bool unreachable = ex is WebException && ((WebException)ex).Response == null && done == 0;
            errors.Add((unreachable ? "!" : "") + name + ": " + ex.Message);
        }
    }

    static void Finish()
    {
        Ui(delegate {
            file.Value = 1; file.Invalidate(); total.Value = 1; total.Invalidate();
            if (errors.Count > 0 && errors[0].StartsWith("!") && done == 0)
            {
                title.Text = "Přehrávač neodpovídá";
                sub.Text = "Zapni v Sonixu Wi-Fi transfer a nech svítit displej.";
            }
            else if (cancel) { title.Text = "Zrušeno"; sub.Text = "Odesláno " + done + " z " + seen + "."; }
            else if (failed == 0) { title.Text = "Hotovo"; sub.Text = done + (done == 1 ? " skladba odeslána" : done < 5 ? " skladby odeslány" : " skladeb odesláno") + " do Music v přehrávači."; }
            else { title.Text = "Hotovo s chybami"; sub.Text = "Odesláno " + done + ", chyb " + failed + ". " + errors[0].TrimStart('!'); }
            count.Text = done + " z " + seen;
            btn.Text = "Zavřít"; btn.Enabled = true; btn.BackColor = Accent; btn.ForeColor = Color.White;
            if (failed == 0 && !cancel)
            {
                System.Windows.Forms.Timer t = new System.Windows.Forms.Timer(); t.Interval = 4000;
                t.Tick += delegate { t.Stop(); form.Close(); }; t.Start();
            }
        });
    }

    static string Clean(string s, string fallback)
    {
        if (string.IsNullOrEmpty(s) || s.EndsWith(":\\") || s.EndsWith(":")) return fallback;
        StringBuilder b = new StringBuilder();
        foreach (char c in s) b.Append(c < 32 || "\\/:*?\"<>|".IndexOf(c) >= 0 ? '_' : c);
        string r = b.ToString().Trim().TrimEnd('.', ' ');
        if (r.Length > 100) r = r.Substring(0, 100).TrimEnd('.', ' ');
        return r.Length == 0 ? fallback : r;
    }

    static void EnsureDir(string baseUrl, string path, HashSet<string> made)
    {
        if (!made.Add(path)) return;
        try
        {
            HttpWebRequest req = (HttpWebRequest)WebRequest.Create(baseUrl + "create");
            req.Method = "POST"; req.Proxy = null; req.Timeout = 15000;
            req.ContentType = "application/x-www-form-urlencoded";
            byte[] body = Encoding.UTF8.GetBytes("path=" + Uri.EscapeDataString(path));
            req.ContentLength = body.Length;
            using (Stream s = req.GetRequestStream()) s.Write(body, 0, body.Length);
            using (req.GetResponse()) { }
        }
        catch (WebException ex) { if (ex.Response == null) { made.Remove(path); throw; } } // HTTP error = already there
    }

    static void PostFile(string baseUrl, string dir, string path)
    {
        string boundary = "----SonixSend" + DateTime.Now.Ticks.ToString("x");
        string name = Path.GetFileName(path);
        byte[] head = Encoding.UTF8.GetBytes("--" + boundary + "\r\nContent-Disposition: form-data; name=\"path\"\r\n\r\n" + dir + "\r\n--" + boundary +
            "\r\nContent-Disposition: form-data; name=\"files[]\"; filename=\"" + name.Replace("\"", "_") + "\"\r\nContent-Type: application/octet-stream\r\n\r\n");
        byte[] tail = Encoding.UTF8.GetBytes("\r\n--" + boundary + "--\r\n");
        HttpWebRequest req = (HttpWebRequest)WebRequest.Create(baseUrl + "upload");
        req.Method = "POST"; req.Proxy = null; req.Timeout = 30 * 60 * 1000; req.ReadWriteTimeout = 5 * 60 * 1000;
        req.AllowWriteStreamBuffering = false; req.ContentType = "multipart/form-data; boundary=" + boundary;
        using (FileStream fs = File.OpenRead(path))
        {
            long len = fs.Length;
            req.ContentLength = head.Length + len + tail.Length;
            using (Stream s = req.GetRequestStream())
            {
                s.Write(head, 0, head.Length);
                byte[] buf = new byte[64 * 1024]; long sent = 0; int r; int lastPct = -1;
                while ((r = fs.Read(buf, 0, buf.Length)) > 0)
                {
                    if (cancel) { req.Abort(); throw new Exception("zrušeno"); }
                    s.Write(buf, 0, r); sent += r;
                    int pct = (int)(sent * 100 / Math.Max(1, len));
                    if (pct != lastPct) { lastPct = pct; double v = pct / 100.0; Ui(delegate { file.Value = v; file.Invalidate(); }); }
                }
                s.Write(tail, 0, tail.Length);
            }
        }
        using (HttpWebResponse res = (HttpWebResponse)req.GetResponse())
            if ((int)res.StatusCode >= 300) throw new Exception("HTTP " + (int)res.StatusCode);
    }
}
