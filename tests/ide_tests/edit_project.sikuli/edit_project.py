""" This test opens, modifies, builds and runs exemple project named "python".
Test succeeds if runtime's stdout behaves as expected
"""

import os
import time

# allow module import from current test directory's parent
addImportPath(os.path.dirname(getBundlePath()))

# common test definitions module
from sikuliberemiz import run_test

def test(app):

    app.doubleClick("1646062660770.png")

    app.WaitIdleUI()

    app.click("example")

    app.WaitIdleUI()

    app.type(Key.DOWN * 10, Key.CTRL)

    app.WaitIdleUI()

    app.doubleClick("1646066996620.png")

    app.WaitIdleUI()

    app.type(Key.TAB*3)  # select text content

    app.type("'sys.stdout.write(\"EDIT TEST OK\\n\")'")

    app.type(Key.ENTER)

    app.WaitIdleUI()

    app.k.Save()

    app.k.Clean()

    app.waitForChangeAndIdleStdout()

    app.k.Build()

    app.waitForChangeAndIdleStdout()

    app.k.Connect()

    app.waitForChangeAndIdleStdout()

    app.k.Transfer()

    app.waitForChangeAndIdleStdout()

    app.k.Run()

    # wait 10 seconds for 10 patterns
    return app.waitPatternInStdout("EDIT TEST OK", 10)

run_test(test, exemple="python")
