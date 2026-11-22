// MusicBee plugin: "Odeslat do přehrávače (Sonix Wi-Fi)" in the track context menu.
// Uploads the selected tracks to the player's Wi-Fi transfer page, always into
// /data/mnt/sd_0/Music/<Album artist>/<Album>/ on the SD card.
// The address is read from mb_SonixSend.txt in MusicBee's persistent storage
// folder (one line, e.g. http://192.168.1.100/), default http://192.168.1.100/.
namespace MusicBeePlugin
{
    using System;
    using System.IO;
    using System.Net;
    using System.Text;
    using System.Threading;
    using System.Windows.Forms;

    public partial class Plugin
    {
        private MusicBeeApiInterface mb;
        private PluginInfo about = new PluginInfo();
        private const string CardMusic = "/data/mnt/sd_0/Music/";
        private const string DefaultUrl = "http://192.168.1.100/";
        private static int busy = 0;

        public PluginInfo Initialise(IntPtr apiInterfacePtr)
        {
            mb = new MusicBeeApiInterface();
            mb.Initialise(apiInterfacePtr);
            about.PluginInfoVersion = PluginInfoVersion;
            about.Name = "Sonix Wi-Fi Send";
            about.Description = "Odesle vybrane skladby do prehravace se Sonixem pres Wi-Fi transfer";
            about.Author = "Sonix";
            about.TargetApplication = "";
            about.Type = PluginType.General;
            about.VersionMajor = 1;
            about.VersionMinor = 0;
            about.Revision = 1;
            about.MinInterfaceVersion = MinInterfaceVersion;
            about.MinApiRevision = MinApiRevision;
            about.ReceiveNotifications = ReceiveNotificationFlags.StartupOnly;
            about.ConfigurationPanelHeight = 0;
            // The track context menu. The item is then moved into the
            // "Odeslat do" / "Send To" submenu the first time the menu opens.
            ToolStripItem item = mb.MB_AddMenuItem("context.Main/Sonix (Wi-Fi)", null, OnSend);
            mb.MB_AddMenuItem("mnuTools/Odeslat do Sonixu (Wi-Fi)", null, OnSend);
            Log("added context item: " + (item == null ? "null" : ("owner=" + (item.Owner == null ? "null" : item.Owner.GetType().Name))));
            if (item != null)
            {
                sendItem = item;
                if (item.Owner != null) HookOwner(item.Owner);
                item.OwnerChanged += delegate (object s, EventArgs a) { if (sendItem.Owner != null) HookOwner(sendItem.Owner); };
            }
            return about;
        }

        private ToolStripItem sendItem;
        private bool moved = false;
        private ToolStrip hooked;

        private void HookOwner(ToolStrip owner)
        {
            if (moved || owner == hooked) return;
            hooked = owner;
            ToolStripDropDown dd = owner as ToolStripDropDown;
            if (dd != null) dd.Opening += delegate (object s, System.ComponentModel.CancelEventArgs a) { MoveIntoSendTo(); };
            owner.ItemAdded += delegate (object s, ToolStripItemEventArgs a) { MoveIntoSendTo(); };
            MoveIntoSendTo();
        }

        private static bool IsSendTo(ToolStripMenuItem m)
        {
            string n = (m.Name ?? "").ToLowerInvariant();
            string t = (m.Text ?? "").Replace("&", "").Trim().ToLowerInvariant();
            return n.Contains("sendto") || t == "odeslat do" || t == "send to";
        }

        private void MoveIntoSendTo()
        {
            if (moved || sendItem == null || sendItem.Owner == null) return;
            System.Text.StringBuilder seen = new System.Text.StringBuilder();
            foreach (ToolStripItem it in sendItem.Owner.Items)
            {
                ToolStripMenuItem m = it as ToolStripMenuItem;
                if (m != null) seen.Append("[" + m.Name + "|" + m.Text + "] ");
                if (m == null || m == sendItem || !IsSendTo(m)) continue;
                moved = true;
                sendItem.Owner.Items.Remove(sendItem);
                m.DropDownItems.Insert(0, sendItem);
                Log("moved into " + m.Name + " / " + m.Text);
                return;
            }
            Log("send-to not found in " + sendItem.Owner.GetType().Name + ": " + seen.ToString());
        }

        private void Log(string line)
        {
            try { File.AppendAllText(Path.Combine(mb.Setting_GetPersistentStoragePath(), "mb_SonixSend.log"), DateTime.Now.ToString("s") + " " + line + "\r\n"); } catch { }
        }

        public bool Configure(IntPtr panelHandle)
        {
            string f = ConfigPath();
            if (!File.Exists(f)) File.WriteAllText(f, DefaultUrl);
            System.Diagnostics.Process.Start("notepad.exe", "\"" + f + "\"");
            return true;
        }

        public void SaveSettings() { }
        public void Close(PluginCloseReason reason) { }
        public void Uninstall() { }
        public void ReceiveNotification(string sourceFileUrl, NotificationType type) { }

        private string ConfigPath()
        {
            return Path.Combine(mb.Setting_GetPersistentStoragePath(), "mb_SonixSend.txt");
        }

        private string BaseUrl()
        {
            try
            {
                string f = ConfigPath();
                if (File.Exists(f))
                {
                    string s = File.ReadAllText(f).Trim();
                    if (s.Length > 0) return s.EndsWith("/") ? s : s + "/";
                }
            }
            catch { }
            return DefaultUrl;
        }

        private void OnSend(object sender, EventArgs e)
        {
            string[] files;
            if (!mb.Library_QueryFilesEx("domain=SelectedFiles", out files) || files == null || files.Length == 0)
            {
                MessageBox.Show("Nejsou vybrané žádné skladby.", "Sonix");
                return;
            }
            if (Interlocked.CompareExchange(ref busy, 1, 0) != 0)
            {
                MessageBox.Show("Odesílání už běží.", "Sonix");
                return;
            }
            // Read the tags on this thread, the upload runs in the background.
            string[] dirs = new string[files.Length];
            for (int i = 0; i < files.Length; i++)
            {
                string artist = mb.Library_GetFileTag(files[i], MetaDataType.AlbumArtist);
                if (string.IsNullOrEmpty(artist)) artist = mb.Library_GetFileTag(files[i], MetaDataType.Artist);
                string album = mb.Library_GetFileTag(files[i], MetaDataType.Album);
                dirs[i] = CardMusic + Clean(artist, "Unknown Artist") + "/" + Clean(album, "Unknown Album") + "/";
            }
            string baseUrl = BaseUrl();
            Thread t = new Thread(delegate () { Upload(baseUrl, files, dirs); });
            t.IsBackground = true;
            t.Start();
        }

        private static string Clean(string s, string fallback)
        {
            if (string.IsNullOrEmpty(s)) return fallback;
            StringBuilder b = new StringBuilder();
            foreach (char c in s)
            {
                if (c < 32 || "\\/:*?\"<>|".IndexOf(c) >= 0) b.Append('_');
                else b.Append(c);
            }
            string r = b.ToString().Trim().TrimEnd('.', ' ');
            if (r.Length > 100) r = r.Substring(0, 100).TrimEnd('.', ' ');
            return r.Length == 0 ? fallback : r;
        }

        private void Upload(string baseUrl, string[] files, string[] dirs)
        {
            int ok = 0;
            StringBuilder errors = new StringBuilder();
            System.Collections.Generic.HashSet<string> made = new System.Collections.Generic.HashSet<string>();
            try
            {
                for (int i = 0; i < files.Length; i++)
                {
                    string name = Path.GetFileName(files[i]);
                    mb.MB_SetBackgroundTaskMessage("Sonix: odesílám " + (i + 1) + "/" + files.Length + " " + name);
                    try
                    {
                        // Music/, Music/Artist/, Music/Artist/Album/ one level at a time.
                        string[] parts = dirs[i].Substring(CardMusic.Length).TrimEnd('/').Split('/');
                        string here = CardMusic;
                        EnsureDir(baseUrl, here, made);
                        foreach (string p in parts)
                        {
                            here += p + "/";
                            EnsureDir(baseUrl, here, made);
                        }
                        PostFile(baseUrl, dirs[i], files[i]);
                        ok++;
                    }
                    catch (Exception ex)
                    {
                        errors.AppendLine(name + ": " + ex.Message);
                    }
                }
            }
            finally
            {
                mb.MB_SetBackgroundTaskMessage("");
                Interlocked.Exchange(ref busy, 0);
            }
            string msg = "Odesláno " + ok + " z " + files.Length + " skladeb do Music v přehrávači.";
            if (errors.Length > 0) msg += "\n\nChyby:\n" + errors.ToString();
            MessageBox.Show(msg, "Sonix");
        }

        private static void EnsureDir(string baseUrl, string path, System.Collections.Generic.HashSet<string> made)
        {
            if (made.Contains(path)) return;
            made.Add(path);
            try
            {
                HttpWebRequest req = (HttpWebRequest)WebRequest.Create(baseUrl + "create");
                req.Method = "POST";
                req.Proxy = null;
                req.Timeout = 15000;
                req.ContentType = "application/x-www-form-urlencoded";
                byte[] body = Encoding.UTF8.GetBytes("path=" + Uri.EscapeDataString(path));
                req.ContentLength = body.Length;
                using (Stream s = req.GetRequestStream()) s.Write(body, 0, body.Length);
                using (req.GetResponse()) { }
            }
            catch (WebException)
            {
                // Usually "already exists"; a real problem shows up on the upload.
            }
        }

        private static void PostFile(string baseUrl, string dir, string file)
        {
            string boundary = "----SonixSend" + DateTime.Now.Ticks.ToString("x");
            string name = Path.GetFileName(file);
            byte[] head = Encoding.UTF8.GetBytes(
                "--" + boundary + "\r\n" +
                "Content-Disposition: form-data; name=\"path\"\r\n\r\n" + dir + "\r\n" +
                "--" + boundary + "\r\n" +
                "Content-Disposition: form-data; name=\"files[]\"; filename=\"" + name.Replace("\"", "_") + "\"\r\n" +
                "Content-Type: application/octet-stream\r\n\r\n");
            byte[] tail = Encoding.UTF8.GetBytes("\r\n--" + boundary + "--\r\n");

            HttpWebRequest req = (HttpWebRequest)WebRequest.Create(baseUrl + "upload");
            req.Method = "POST";
            req.Proxy = null;
            req.Timeout = 30 * 60 * 1000;
            req.ReadWriteTimeout = 5 * 60 * 1000;
            req.AllowWriteStreamBuffering = false;
            req.SendChunked = false;
            req.ContentType = "multipart/form-data; boundary=" + boundary;
            using (FileStream fs = File.OpenRead(file))
            {
                req.ContentLength = head.Length + fs.Length + tail.Length;
                using (Stream s = req.GetRequestStream())
                {
                    s.Write(head, 0, head.Length);
                    fs.CopyTo(s, 64 * 1024);
                    s.Write(tail, 0, tail.Length);
                }
            }
            using (HttpWebResponse res = (HttpWebResponse)req.GetResponse())
            {
                if ((int)res.StatusCode < 200 || (int)res.StatusCode >= 300)
                    throw new Exception("HTTP " + (int)res.StatusCode);
            }
        }
    }
}
