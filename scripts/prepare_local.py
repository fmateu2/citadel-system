"""Create isolated sample inventories and localhost configurations for five realms."""

import argparse
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
REALMS = ("Stark", "Lannister", "Targaryen", "Greyjoy", "Tully")


def prepare(output, base_port=18890):
    output = Path(output).resolve()
    if output.exists():
        raise FileExistsError(f"Destination already exists: {output}")
    if not 1024 <= base_port <= 65531:
        raise ValueError("Choose a starting port between 1024 and 65531.")
    output.mkdir(parents=True)
    for index, realm in enumerate(REALMS):
        destination = output / realm
        destination.mkdir()
        source = ROOT / "data" / realm
        shutil.copy2(source / f"{realm.lower()}_stock.db", destination / "inventory.db")
        shutil.copy2(source / f"{realm.lower()}.png", destination / f"{realm.lower()}.png")
        lines = [
            realm,
            str(destination),
            "2",
            "127.0.0.1",
            str(base_port + index),
            "--- ROUTES ---",
        ]
        lines.extend(
            f"{other} 127.0.0.1 {base_port + offset}"
            for offset, other in enumerate(REALMS)
            if other != realm
        )
        (destination / "config.dat").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return output


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / ".demo")
    parser.add_argument("--base-port", type=int, default=18890)
    args = parser.parse_args()
    folder = prepare(args.output, args.base_port)
    print(f"Created five local configurations in {folder}")
    print("Run each realm in its own terminal, from the project root:")
    for realm in REALMS[:2]:
        print(f'./Maester "{folder / realm / "config.dat"}" "{folder / realm / "inventory.db"}"')
