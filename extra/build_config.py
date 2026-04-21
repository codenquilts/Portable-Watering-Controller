Import("env")
from pathlib import Path
import os

build_dir = Path(env.subst("$PROJECT_SRC_DIR"))
out_path = build_dir / "build_config.h"

smtp_host = os.environ.get("SMTP_HOST", "")
smtp_port = os.environ.get("SMTP_PORT", "587")
smtp_user = os.environ.get("SMTP_USER", "")
smtp_pass = os.environ.get("SMTP_PASS", "")
smtp_from = os.environ.get("SMTP_FROM", "")
smtp_use_ssl = os.environ.get("SMTP_USE_SSL", "0")

content = f"""#pragma once
#define SMTP_HOST \"{smtp_host}\"
#define SMTP_PORT {smtp_port}
#define SMTP_USER \"{smtp_user}\"
#define SMTP_PASS \"{smtp_pass}\"
#define SMTP_FROM \"{smtp_from}\"
#define SMTP_USE_SSL {smtp_use_ssl}
"""

out_path.write_text(content)
