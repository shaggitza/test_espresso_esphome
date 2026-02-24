import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate
from esphome.const import CONF_ID

CODEOWNERS = ["@shaggitza"]
DEPENDENCIES = ["climate"]

espresso_machine_heater_ns = cg.esphome_ns.namespace("espresso_machine_heater")
EspressoMachineHeater = espresso_machine_heater_ns.class_(
    "EspressoMachineHeater", cg.Component
)

CONF_CLIMATE_ID = "climate_id"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(EspressoMachineHeater),
        cv.Required(CONF_CLIMATE_ID): cv.use_id(climate.Climate),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    climate_entity = await cg.get_variable(config[CONF_CLIMATE_ID])
    cg.add(var.set_climate(climate_entity))
