import renderdoc as rd
from typing import List
import rdtest


class D3D12_PrimitiveID(rdtest.TestCase):
    demos_test_name = 'D3D12_PrimitiveID'
    
    def test_action(self, action: rd.ActionDescription, x, y, prim, expected_prim, expected_output):
        self.controller.SetFrameEvent(action.eventId, True)
        pipe: rd.PipeState = self.controller.GetPipelineState()

        if not pipe.GetShaderReflection(rd.ShaderStage.Pixel).debugInfo.debuggable:
            rdtest.log.print("Skipping undebuggable shader at {}.".format(action.eventId))
            return

        inputs = rd.DebugPixelInputs()
        inputs.primitive = prim
        trace: rd.ShaderDebugTrace = self.controller.DebugPixel(x, y, inputs)

        cycles, variables = self.process_trace(trace)

        # Find the SV_PrimitiveID variable
        primInput = self.find_input_source_var(trace, rd.ShaderBuiltin.PrimitiveIndex)
        if primInput is None:
            # If we didn't find it, then we should be expecting a 0
            if len(expected_prim) > 1 or expected_prim[0] != 0:
                rdtest.log.error("Expected prim {} at {},{} did not match actual prim {}.".format(
                    str(expected_prim), x, y, prim))
                return False
        else:
            # Look up the matching register in the inputs, and see if the expected value matches
            inputs: List[rd.ShaderVariable] = list(trace.inputs)
            primInputName = primInput.variables[0].name
            if inputs[0].name.startswith('_IN') and primInputName.startswith('_IN.'):
                # Walk the DXIL input structure
                inputVars = inputs[0].members
                # Remove the input name prefix
                primInputName = primInputName[4:]
            else:
                inputVars = inputs

            primVars = [var for var in inputVars if var.name == primInputName]

            primValue = primVars[0]
            if primValue.value.u32v[0] not in expected_prim:
                rdtest.log.error("Expected prim {} at {},{} did not match actual prim {}.".format(
                    str(expected_prim), x, y, primValue.value.u32v[0]))
                return False

        # Compare shader debug output against an expected value instead of the RT's output,
        # since we're testing overlapping primitives in a single action
        if expected_output is not None:
            output = self.find_output_source_var(trace, rd.ShaderBuiltin.ColorOutput, 0)
            debugged = self.evaluate_source_var(output, variables)
            if list(debugged.value.f32v[0:4]) != expected_output:
                rdtest.log.error("Expected value {} at {},{} did not match actual {}.".format(
                    expected_output, x, y, debugged.value.f32v[0:4]))
                return False

        self.controller.FreeTrace(trace)

        rdtest.log.success("Test at {},{} matched as expected".format(x, y))
        return True

    def check_capture(self):
        if not self.controller.GetAPIProperties().shaderDebugging:
            rdtest.log.success("Shader debugging not enabled, skipping test")
            return

        success = True

        markers = ["SM5.0", "SM6.0"]
        for i in range(2):
            rdtest.log.begin_section(markers[i])
            # Jump to the action
            test_marker: rd.ActionDescription = self.find_action(markers[i])
            if test_marker is None:
                rdtest.log.print(f"No {markers[i]} actions to test")
                return

            y = 40 + i * 150
            # Draw 1: No GS, PS without prim
            action = test_marker.next
            success &= self.test_action(action, 100, y, rd.ReplayController.NoPreference, [0], [0, 1, 0, 1])

            # Draw 2: No GS, PS with prim
            action = action.next
            success &= self.test_action(action, 300, y, rd.ReplayController.NoPreference, [0], [0, 1, 0, 1])

            # Draw 3: GS, PS without prim
            y = 125 + i * 150
            action = action.next
            success &= self.test_action(action, 125, y, rd.ReplayController.NoPreference, [0], [0, 1, 0, 1])

            # Draw 4: GS, PS with prim
            action = action.next
            success &= self.test_action(action, 325, y, 2, [2], [0.5, 1, 0, 1])
            success &= self.test_action(action, 325, y, 3, [3], [0.75, 1, 0, 1])
            # No expected output here, since it's nondeterministic which primitive gets selected
            success &= self.test_action(action, 325, y, rd.ReplayController.NoPreference, [2, 3], None)
            rdtest.log.end_section(markers[i])

        if not success:
            raise rdtest.TestFailureException("Some tests were not as expected")

        rdtest.log.success("All tests matched")
