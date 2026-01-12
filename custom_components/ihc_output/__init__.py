import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.const import CONF_ID, CONF_PIN

ihc_ns = cg.esphome_ns.namespace("ihc_output")
IHCOutputComponent = ihc_ns.class_("IHCOutputComponent", cg.Component)


CONFIG_SCHEMA = cv.ensure_list(
    cv.Schema({
        cv.GenerateID(): cv.declare_id(IHCOutputComponent),
        cv.Required(CONF_PIN): pins.gpio_output_pin_schema,
    }).extend(cv.COMPONENT_SCHEMA)
)

async def to_code(config_list):
    
    for config in config_list:
        var = cg.new_Pvariable(config[CONF_ID])
        await cg.register_component(var, config)
        
        pin = await cg.gpio_pin_expression(config[CONF_PIN])

        cg.add(var.set_pin(pin))
