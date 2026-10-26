import os
import sys

if getattr(sys, "frozen", False):
    frozen_root = getattr(sys, "_MEIPASS", "")
    os.environ["TCL_LIBRARY"] = os.path.join(frozen_root, "_tcl_data").replace("\\", "/")
    os.environ["TK_LIBRARY"] = os.path.join(frozen_root, "_tk_data").replace("\\", "/")

from tkinter import Tk, messagebox

from aperolspritz.database import MeasurementDatabase
from aperolspritz.settings import SettingsStore, application_data_dir
from aperolspritz.ui import MonitorWindow


def main() -> None:
    root = Tk()
    try:
        settings = SettingsStore()
        window = MonitorWindow(root, settings, MeasurementDatabase())
    except Exception as error:
        root.withdraw()
        messagebox.showerror("AperolSpritz Monitor", "Не удалось запустить приложение:\n{}".format(error))
        root.destroy()
        return
    if os.environ.get("APEROLSPRITZ_SMOKE_TEST") == "1":
        settings.save(window.devices)
        (application_data_dir() / "smoke-ready.txt").write_text("ready\n", encoding="utf-8")
        root.after(250, window.close)
    root.mainloop()


if __name__ == "__main__":
    main()
