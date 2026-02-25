"""Pydantic models for NovaBridge plan/runtime payloads."""

from __future__ import annotations

from typing import Any, Dict, List, Literal, Optional, Union

from pydantic import BaseModel, ConfigDict, Field


class Vector3(BaseModel):
    x: float
    y: float
    z: float


class Rotator3(BaseModel):
    pitch: float
    yaw: float
    roll: float


class Transform(BaseModel):
    location: Optional[Union[Vector3, List[float]]] = None
    rotation: Optional[Union[Rotator3, List[float], Vector3]] = None
    scale: Optional[Union[Vector3, List[float]]] = None


class SpawnParams(BaseModel):
    model_config = ConfigDict(extra="allow")

    actor_class: Optional[str] = Field(default=None, alias="class")
    type: Optional[str] = None
    label: Optional[str] = None
    x: Optional[float] = None
    y: Optional[float] = None
    z: Optional[float] = None
    pitch: Optional[float] = None
    yaw: Optional[float] = None
    roll: Optional[float] = None
    transform: Optional[Transform] = None


class DeleteParams(BaseModel):
    model_config = ConfigDict(extra="allow")

    name: Optional[str] = None
    target: Optional[str] = None


class SetParams(BaseModel):
    model_config = ConfigDict(extra="allow")

    target: Optional[str] = None
    name: Optional[str] = None
    props: Dict[str, Any] = Field(default_factory=dict)


class CallParams(BaseModel):
    model_config = ConfigDict(extra="allow")

    target: Optional[str] = None
    name: Optional[str] = None
    function: Optional[str] = None
    event: Optional[str] = None
    args: Optional[Union[List[Any], Dict[str, Any]]] = None


class ScreenshotParams(BaseModel):
    model_config = ConfigDict(extra="allow")

    width: Optional[int] = None
    height: Optional[int] = None
    inline: Optional[bool] = None
    return_base64: Optional[bool] = None
    format: Optional[Literal["png", "raw"]] = None


PlanAction = Literal["spawn", "delete", "set", "call", "screenshot"]


class PlanStep(BaseModel):
    action: PlanAction
    params: Dict[str, Any] = Field(default_factory=dict)


class ExecutePlanRequest(BaseModel):
    plan_id: Optional[str] = None
    mode: Optional[Literal["editor", "runtime"]] = None
    role: Optional[Literal["admin", "automation", "read_only"]] = None
    steps: List[PlanStep]


class StepResult(BaseModel):
    model_config = ConfigDict(extra="allow")

    step: int
    status: Literal["success", "error"]
    message: str
    object_id: Optional[str] = None


class ExecutePlanResponse(BaseModel):
    model_config = ConfigDict(extra="allow")

    status: str
    plan_id: Optional[str] = None
    mode: Optional[str] = None
    role: Optional[str] = None
    step_count: Optional[int] = None
    success_count: Optional[int] = None
    error_count: Optional[int] = None
    results: List[StepResult] = Field(default_factory=list)


class HealthResponse(BaseModel):
    model_config = ConfigDict(extra="allow")

    status: str
    version: Optional[str] = None
    mode: Optional[str] = None
    engine: Optional[str] = None
    project_name: Optional[str] = None
    port: Optional[int] = None
    routes: Optional[int] = None


class CapabilityRecord(BaseModel):
    model_config = ConfigDict(extra="allow")

    action: str
    roles: List[str] = Field(default_factory=list)


class CapsResponse(BaseModel):
    model_config = ConfigDict(extra="allow")

    status: str
    mode: Optional[str] = None
    role: Optional[str] = None
    version: Optional[str] = None
    capabilities: List[CapabilityRecord] = Field(default_factory=list)
