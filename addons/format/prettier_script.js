/*
    SPDX-FileCopyrightText: 2022 Waqar Ahmed <waqar.17a@gmail.com>
    SPDX-FileCopyrightText: 2022 Abdullah <abdullahatta@streetwriters.co>
    SPDX-License-Identifier: LGPL-2.0-or-later
*/
const path = require("path");
const childProcess = require("child_process");

process.stdin.resume();
process.stdin.setEncoding("utf8");

var text = "";

process.stdin.on("data", (data) => {
  try {
    text += data;
    // read till we find a null char
    if (text[text.length - 1] !== "\0") {
      return;
    }

    const { filePath, source, cursorOffset } = JSON.parse(text.slice(0, -1));
    const prettier = getPrettier(filePath);
    text = "";

    if (!prettier) {
      const formatted = prettify(filePath, cursorOffset);
      if (formatted) {
        log(formatted);
        return;
      }
      return;
    }

    if (!prettier) {
      console.error("Prettier not found.");
      return;
    }

    const options =
      prettier.resolveConfig.sync(filePath, {
        useCache: false,
        editorconfig: true,
      }) || {};

    log(
      prettier.formatWithCursor(source, {
        cursorOffset: parseInt(cursorOffset),
        filepath: filePath,
        ...options,
      })
    );
  } catch (e) {
    console.error(e);
  }
});

function tryGetPrettier(lookupPath) {
  try {
    return require(require.resolve("prettier", { paths: [lookupPath] }));
  } catch (e) {
    return null;
  }
}

function tryGetRootPath(command) {
  var globalRootPath = null;
  return function () {
    if (globalRootPath) return globalRootPath;
    try {
      return (globalRootPath = childProcess
        .execSync(command, {
          encoding: "utf8",
        })
        .trim());
    } catch (e) {
      return null;
    }
  };
}

const getVoltaNodeModulesPath = () =>
  process.env.VOLTA_HOME
    ? path.join(
        process.env.VOLTA_HOME,
        "tools",
        "image",
        "packages",
        "prettier",
        "lib"
      )
    : null;
const getNpmGlobalRootPath = tryGetRootPath("npm --offline root -g");
const getYarnGlobalRootPath = tryGetRootPath("yarn --offline global dir");

const rootPathChecks = [
  path.dirname,
  getVoltaNodeModulesPath,
  getNpmGlobalRootPath,
  getYarnGlobalRootPath,
];

function getPrettier(filePath) {
  let prettier = null;
  for (const check of rootPathChecks) {
    const lookupPath = check(filePath);
    if (!lookupPath) continue;

    prettier = tryGetPrettier(lookupPath);
    if (prettier) return prettier;
  }
}

function prettify(filePath, cursorOffset) {
  try {
    const { stdout, stderr, status } = childProcess.spawnSync(
      "prettier",
      ["--cursor-offset", cursorOffset, filePath],
      {
        encoding: "utf8",
      }
    );

    if (status !== 0) {
        process.stderr.write(stderr);
        return null;
    }

    const resultCursorOffset = parseInt(stderr);
    return {
      formatted: stdout,
      cursorOffset: isNaN(resultCursorOffset)
        ? cursorOffset
        : resultCursorOffset,
    };
  } catch (e) {
    console.error(e);
    return null;
  }
}

function log(message) {
  process.stdout.write(JSON.stringify(message));
  process.stdout.write("[[{END_PRETTIER_SCRIPT}]]");
}
