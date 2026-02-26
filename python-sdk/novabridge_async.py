"""Async NovaBridge client with retries and shared aiohttp session support."""

from __future__ import annotations

import asyncio
import json
import urllib.parse
from dataclasses import dataclass
from typing import Any, Dict, Optional

import aiohttp


class AsyncNovaBridgeError(RuntimeError):
    """Raised when async NovaBridge requests fail."""


@dataclass
class AsyncNovaBridge:
    host: str = "localhost"
    port: int = 30010
    assistant_host: Optional[str] = None
    assistant_port: int = 30016
    timeout: int = 60
    api_key: Optional[str] = None
    role: Optional[str] = None
    runtime_token: Optional[str] = None
    max_retries: int = 2
    retry_backoff: float = 0.25

    _session: Optional[aiohttp.ClientSession] = None
    _owns_session: bool = False

    @property
    def base_url(self) -> str:
        return f"http://{self.host}:{self.port}/nova"

    @property
    def assistant_base_url(self) -> str:
        host = self.assistant_host or self.host
        return f"http://{host}:{self.assistant_port}/assistant"

    async def __aenter__(self) -> "AsyncNovaBridge":
        await self._ensure_session()
        return self

    async def __aexit__(self, exc_type, exc, tb) -> None:
        await self.close()

    async def close(self) -> None:
        if self._session and self._owns_session:
            await self._session.close()
        self._session = None
        self._owns_session = False

    async def _ensure_session(self) -> aiohttp.ClientSession:
        if self._session is not None:
            return self._session
        timeout = aiohttp.ClientTimeout(total=float(self.timeout))
        self._session = aiohttp.ClientSession(timeout=timeout)
        self._owns_session = True
        return self._session

    def _build_headers(
        self,
        *,
        role: Optional[str] = None,
        runtime_token: Optional[str] = None,
    ) -> Dict[str, str]:
        headers: Dict[str, str] = {}
        if self.api_key:
            headers["X-API-Key"] = self.api_key
        effective_role = role if role is not None else self.role
        if effective_role:
            headers["X-NovaBridge-Role"] = effective_role
        effective_token = runtime_token if runtime_token is not None else self.runtime_token
        if effective_token:
            headers["X-NovaBridge-Token"] = effective_token
        return headers

    async def _request(
        self,
        method: str,
        route: str,
        data: Optional[Dict[str, Any]] = None,
        *,
        role: Optional[str] = None,
        runtime_token: Optional[str] = None,
        expect_bytes: bool = False,
        base_url_override: Optional[str] = None,
    ) -> Any:
        session = await self._ensure_session()
        base = base_url_override or self.base_url
        url = f"{base}{route}"
        headers = self._build_headers(role=role, runtime_token=runtime_token)

        last_error: Optional[Exception] = None
        for attempt in range(self.max_retries + 1):
            try:
                async with session.request(method, url, json=data, headers=headers) as resp:
                    payload = await resp.read()
                    if resp.status >= 400:
                        detail = payload.decode("utf-8", errors="replace")
                        if resp.status in (429, 500, 502, 503, 504) and attempt < self.max_retries:
                            await asyncio.sleep(self.retry_backoff * (2**attempt))
                            continue
                        raise AsyncNovaBridgeError(f"HTTP {resp.status}: {detail}")

                    if expect_bytes:
                        return payload

                    if not payload:
                        return {"status": "ok"}
                    try:
                        return json.loads(payload)
                    except json.JSONDecodeError as exc:
                        raise AsyncNovaBridgeError(f"Invalid JSON response: {exc}") from exc
            except (aiohttp.ClientError, asyncio.TimeoutError) as exc:
                last_error = exc
                if attempt < self.max_retries:
                    await asyncio.sleep(self.retry_backoff * (2**attempt))
                    continue
                raise AsyncNovaBridgeError(f"Connection failed: {exc}") from exc

        if last_error:
            raise AsyncNovaBridgeError(f"Connection failed: {last_error}")
        raise AsyncNovaBridgeError("Request failed")

    async def _assistant_request(
        self,
        method: str,
        route: str,
        data: Optional[Dict[str, Any]] = None,
    ) -> Dict[str, Any]:
        result = await self._request(
            method,
            route,
            data=data,
            role="",
            runtime_token="",
            expect_bytes=False,
            base_url_override=self.assistant_base_url,
        )
        if not isinstance(result, dict):
            raise AsyncNovaBridgeError("Assistant endpoint returned non-JSON object")
        return result

    async def health(self) -> Dict[str, Any]:
        return await self._request("GET", "/health")

    async def caps(self) -> Dict[str, Any]:
        return await self._request("GET", "/caps")

    async def assistant_health(self) -> Dict[str, Any]:
        return await self._assistant_request("GET", "/health")

    async def assistant_catalog(self) -> Dict[str, Any]:
        return await self._assistant_request("GET", "/catalog")

    async def assistant_plan(self, prompt: str, *, mode: str = "editor") -> Dict[str, Any]:
        payload = {
            "prompt": prompt,
            "mode": "runtime" if mode == "runtime" else "editor",
        }
        return await self._assistant_request("POST", "/plan", payload)

    async def assistant_execute(
        self,
        plan: Dict[str, Any],
        *,
        allow_high_risk: bool = False,
        risk: Optional[Dict[str, Any]] = None,
    ) -> Dict[str, Any]:
        payload: Dict[str, Any] = {
            "plan": plan,
            "allow_high_risk": bool(allow_high_risk),
        }
        if risk is not None:
            payload["risk"] = risk
        return await self._assistant_request("POST", "/execute", payload)

    async def scene_list(self) -> Dict[str, Any]:
        return await self._request("GET", "/scene/list")

    async def execute_plan(self, steps: Any, *, plan_id: Optional[str] = None, role: Optional[str] = None) -> Dict[str, Any]:
        data: Dict[str, Any] = {"steps": steps}
        if plan_id is not None:
            data["plan_id"] = plan_id
        if role is not None:
            data["role"] = role
        return await self._request("POST", "/executePlan", data=data, role=role)

    async def spawn(
        self,
        actor_class: str,
        *,
        x: float = 0.0,
        y: float = 0.0,
        z: float = 0.0,
        pitch: float = 0.0,
        yaw: float = 0.0,
        roll: float = 0.0,
        label: Optional[str] = None,
    ) -> Dict[str, Any]:
        data: Dict[str, Any] = {
            "class": actor_class,
            "x": x,
            "y": y,
            "z": z,
            "pitch": pitch,
            "yaw": yaw,
            "roll": roll,
        }
        if label:
            data["label"] = label
        return await self._request("POST", "/scene/spawn", data=data)

    async def delete_actor(self, name: str) -> Dict[str, Any]:
        return await self._request("POST", "/scene/delete", data={"name": name})

    async def undo(self, *, role: Optional[str] = None) -> Dict[str, Any]:
        return await self._request("POST", "/undo", data={}, role=role)

    async def runtime_pair(self, code: str, *, role: Optional[str] = None) -> Dict[str, Any]:
        body: Dict[str, Any] = {"code": code}
        if role:
            body["role"] = role
        result = await self._request("POST", "/runtime/pair", data=body, runtime_token="")
        token = result.get("token")
        if isinstance(token, str) and token:
            self.runtime_token = token
        return result

    async def viewport_screenshot(
        self,
        *,
        width: Optional[int] = None,
        height: Optional[int] = None,
        raw: bool = False,
    ) -> Any:
        query: Dict[str, Any] = {}
        if width is not None:
            query["width"] = int(width)
        if height is not None:
            query["height"] = int(height)
        if raw:
            query["format"] = "raw"

        route = "/viewport/screenshot"
        if query:
            route = f"{route}?{urllib.parse.urlencode(query)}"

        return await self._request("GET", route, expect_bytes=raw)
