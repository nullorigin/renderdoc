import rdtest
import renderdoc as rd


class D3D12_Draw_Zoo(rdtest.Draw_Zoo):
    demos_test_name = 'D3D12_Draw_Zoo'
    internal = False

    def check_capture(self):
        rdtest.log.begin_section("SM5.0")
        marker: rd.ActionDescription = self.find_action("SM5.0")
        self.check_capture_action(marker)
        rdtest.log.end_section("SM5.0")

        rdtest.log.begin_section("SM6.0")
        marker: rd.ActionDescription = self.find_action("SM6.0")
        if marker is None:
            rdtest.log.print("No SM6.0 action to test")
            return
        self.check_capture_action(marker)
        rdtest.log.end_section("SM6.0")

        rdtest.log.begin_section("SM6.6")
        marker: rd.ActionDescription = self.find_action("SM6.6")
        if marker is None:
            rdtest.log.print("No SM6.6 action to test")
            return
        self.check_capture_action(marker)
        rdtest.log.end_section("SM6.6")