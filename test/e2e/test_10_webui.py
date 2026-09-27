"""E2E: embedded web UI flows, driven by a headless browser.

These tests need Playwright (uv dependency) and its chromium browser;
without them the module skips so bare/CI runs stay clean. They exercise
the single-page UI served at ``/`` end to end: status panel, capture
controls, file browser, log viewer, and the config editor.
"""
import re
import time

import pytest

pw = pytest.importorskip(
    "playwright", reason="Playwright is not installed; UI coverage skipped"
)

from conftest import COUNTER_POLL_S, COUNTER_TIMEOUT_S  # noqa: E402

UI_TIMEOUT_MS = 10000


@pytest.fixture(scope="module")
def page(client):
    """Headless chromium page authenticated against the device UI."""
    from playwright.sync_api import sync_playwright

    with sync_playwright() as p:
        try:
            browser = p.chromium.launch(headless=True)
        except Exception as e:
            pytest.skip(
                "the chromium browser is not installed; run "
                "`uv run playwright install chromium` once to enable "
                f"the UI coverage (launch failed: {e})"
            )
        context = browser.new_context(
            http_credentials={"username": "admin", "password": client.password}
        )
        page = context.new_page()
        page.set_default_timeout(UI_TIMEOUT_MS)
        yield page
        context.close()
        browser.close()


def _goto_ui(page, client):
    """Open the device UI and wait for the first status render."""
    page.goto(f"http://{client.host}:{client.port}/")
    page.wait_for_function(
        "document.getElementById('st-free').textContent !== '-'"
    )


def test_ui_page_loads_and_status_panel_populates(client, page):
    """The page loads and the status panel renders live telemetry."""
    _goto_ui(page, client)
    assert "Micro Security Cam" in page.title()
    badge = page.locator("#sd-badge").text_content()
    assert badge in ("SD ready", "SD missing")
    assert page.locator("#st-frames").text_content() not in ("-", "")
    assert page.locator("#st-free").text_content() not in ("-", "")
    if client.status()["sd"]:
        assert badge == "SD ready"


def test_ui_record_control_drives_device(client, guard, capture_ready, page):
    """The record button starts and stops recording on the device."""
    if capture_ready:
        pytest.skip(capture_ready)
    status = client.status()
    if not status["sd"]:
        pytest.skip("recording requires the SD card")
    assert not status["recording"], "device already recording before test"

    _goto_ui(page, client)
    page.click("#btn-rec-start")
    page.wait_for_function(
        "document.getElementById('st-rec').textContent === 'REC'",
        timeout=UI_TIMEOUT_MS,
    )
    guard.m_started_recording = True  # registered for teardown stop
    try:
        page.wait_for_function(
            "document.getElementById('st-frames').textContent !== '0' "
            "&& document.getElementById('st-frames').textContent !== '-'",
            timeout=UI_TIMEOUT_MS,
        )
    finally:
        page.click("#btn-rec-stop")
    page.wait_for_function(
        "document.getElementById('st-rec').textContent === 'off'",
        timeout=UI_TIMEOUT_MS,
    )
    guard.m_started_recording = False


def test_ui_stream_control_drives_device_and_renders_mjpeg(
    client, guard, capture_ready, page
):
    """The preview button starts streaming and the img element decodes
    MJPEG frames; stopping clears both the flag and the preview."""
    if capture_ready:
        pytest.skip(capture_ready)
    # A preview running before this test (e.g. restored by the session
    # preflight) must survive it: snapshot and restore it here, without
    # registering with the guard (which would stop it at teardown).
    was_streaming = bool(client.status().get("streaming"))

    _goto_ui(page, client)
    try:
        page.click("#btn-stream-start")
        page.wait_for_function(
            "document.getElementById('st-stream').textContent === 'live'",
            timeout=UI_TIMEOUT_MS,
        )
        guard.m_started_streaming = True  # registered for teardown stop
        try:
            # The MJPEG multipart stream must actually render: the browser
            # only sets naturalWidth once a frame has decoded.
            page.wait_for_function(
                "document.getElementById('preview-img').naturalWidth > 0",
                timeout=20000,
            )
        finally:
            page.click("#btn-stream-stop")
        page.wait_for_function(
            "document.getElementById('st-stream').textContent === 'off'",
            timeout=UI_TIMEOUT_MS,
        )
        guard.m_started_streaming = False
        assert page.locator("#preview-img").get_attribute("src") == ""
    finally:
        if was_streaming:
            client.control("stream_start")


def test_ui_photo_control_reports_saved_path(client, guard, capture_ready, page):
    """The photo button reports the saved photo path in the UI."""
    if capture_ready:
        pytest.skip(capture_ready)
    status = client.status()
    if not status["sd"]:
        pytest.skip("photo capture requires the SD card")

    _goto_ui(page, client)
    page.click("#btn-photo")
    page.wait_for_function(
        "document.getElementById('ctrl-msg').textContent.startsWith"
        "('photo: /')",
        timeout=UI_TIMEOUT_MS,
    )


def test_ui_file_browser_lists_and_navigates(client, guard, page):
    """The file browser lists the card root and navigates directories."""
    status = client.status()
    if not status["sd"]:
        pytest.skip("the file browser requires the SD card")

    _goto_ui(page, client)
    page.wait_for_function(
        "document.querySelectorAll('#file-list li').length > 0",
        timeout=UI_TIMEOUT_MS,
    )
    assert page.locator("#crumb").text_content() == "/"
    assert "error" not in page.locator("#file-list").inner_text().lower(), (
        "file list rendered an error: "
        + page.locator("#file-list").inner_text()
    )
    assert page.locator("#file-list li.dir").count() > 0, \
        "no directory entries in the root listing"

    date_dirs = page.locator("#file-list li.dir span.name").filter(
        has_text=re.compile(r"^\d{8}$")
    )
    if date_dirs.count() == 0:
        pytest.skip("no date directory on the card to navigate into")
    date_dirs.first.click()
    page.wait_for_function(
        "document.getElementById('crumb').textContent !== '/'",
        timeout=UI_TIMEOUT_MS,
    )
    page.click("#btn-up")
    page.wait_for_function(
        "document.getElementById('crumb').textContent === '/'",
        timeout=UI_TIMEOUT_MS,
    )


def test_ui_log_viewer_loads_entries(client, page):
    """The log viewer lists log files and loads a file's tail."""
    resp = client.get("/logs?start=0")
    if resp.status != 200 or not resp.json()["total"]:
        pytest.skip(
            "no SD log files are being produced by this firmware build "
            "(/logs is empty or absent), so log-viewer coverage cannot run"
        )

    _goto_ui(page, client)
    page.wait_for_function(
        "document.querySelectorAll('#log-list li').length > 0",
        timeout=UI_TIMEOUT_MS,
    )
    page.locator("#log-list li span.name").first.click()
    page.wait_for_function(
        "document.getElementById('log-view').textContent.length > 0 "
        "&& document.getElementById('log-view').textContent.startsWith"
        "('Loading') === false",
        timeout=UI_TIMEOUT_MS,
    )


def test_ui_config_editor_loads_and_saves(client, guard, page):
    """The config editor loads the live INI and saves it unchanged."""
    _goto_ui(page, client)
    page.click("#btn-config-load")
    page.wait_for_function(
        "document.querySelector('textarea#config').value.length > 0",
        timeout=UI_TIMEOUT_MS,
    )
    ini_text = page.locator("textarea#config").input_value()
    assert "[" in ini_text and "=" in ini_text, "config editor shows no INI"

    page.click("#btn-config-save")
    page.wait_for_function(
        "document.getElementById('cfg-msg').textContent.length > 0",
        timeout=UI_TIMEOUT_MS,
    )
    assert page.locator("#cfg-msg").text_content() == "saved (nvs)", (
        f"config save reported: {page.locator('#cfg-msg').text_content()!r}"
    )


def test_ui_photos_counter_reflects_capture(client, guard, capture_ready, page):
    """After a UI photo, the status panel's photos count ticks up."""
    if capture_ready:
        pytest.skip(capture_ready)
    status = client.status()
    if not status["sd"]:
        pytest.skip("photo capture requires the SD card")
    before = status["photos"]

    _goto_ui(page, client)
    guard.take_photo()  # the same op the button issues, wedge-aware
    deadline = time.monotonic() + COUNTER_TIMEOUT_S
    while time.monotonic() < deadline:
        # The panel refreshes every 5 s against the device's 60 s cache.
        shown = page.locator("#st-photos").text_content()
        if shown not in ("-", "") and int(shown) > before:
            return
        page.wait_for_timeout(COUNTER_POLL_S * 1000)
    pytest.fail(
        f"status panel never showed the incremented photo count "
        f"(before={before})"
    )
