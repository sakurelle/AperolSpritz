from tkinter import Tk, messagebox

from aperolspritz.database import MeasurementDatabase
from aperolspritz.settings import SettingsStore
from aperolspritz.ui import MonitorWindow


def main() -> None:
    root = Tk()
    try:
        MonitorWindow(root, SettingsStore(), MeasurementDatabase())
    except Exception as error:
        root.withdraw()
        messagebox.showerror("AperolSpritz Monitor", "Не удалось запустить приложение:\n{}".format(error))
        root.destroy()
        return
    root.mainloop()


if __name__ == "__main__":
    main()
